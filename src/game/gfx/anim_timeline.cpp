// The animation timeline (Gfx2D::CAnimTimeline, anim_controller.h), the blend channel's timeline (blend_timeline.h),
// the base animation controller's members and the base animation event's destructor body
// (0x00481380-0x004819E6, but 0x004817D0). From the disassembly.

#include <cstdint>

#include "game/gfx/anim_track.h"
#include "game/gfx/custom_anim_key.h"

#include "game/core/arena_alloc.h"
#include "game/core/mem.h"
#include "game/obj/entity.h"
#include "game/obj/skeleton_component.h"
#include "game/script/param_struct.h"
#include "game/sys/stopwatch.h"
#include "game/core/code_address.h"
#include "game/core/game_vtables.h"
#include "game/skater/vehicle_sound.h"
#include "game/core/stubs.h"

namespace Gfx2D {

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

MemSys::ListNode* Elem(CAnimTimeline* timeline) { return reinterpret_cast<MemSys::ListNode*>(timeline); }

// The timeline's empty state (Reset and the constructor).
void Clear(CAnimTimeline* t)
{
    t->m_outset = 0.0f;
    t->m_ongoing = 0.0f;
    t->m_finish = 0.0f;
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
CAnimTimeline* CAnimTimeline::MakeNew()
{
    MemSys::ListNode* node = Elem(this);
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
    Elem(this)->UnlinkAs(kListNodeVtable);
}

// 0x004816E0
CAnimTimeline* CAnimTimeline::Teardown(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x00481380
void CAnimTimeline::Reset()
{
    Clear(this);
}

// 0x00481640
void CAnimTimeline::TickFrame()
{
    m_ongoing = static_cast<float>(NextTime());
}

// 0x00481430 (start: the smaller of the two - `to` when they're equal or NaN; end: the other)
void CAnimTimeline::PlayAnim(uint32_t anim, float from, float to, int loopType, float /*blendPeriod*/, float speed)
{
    bool forwards = from < to;
    m_outset = forwards ? from : to;
    m_ongoing = from;
    m_finish = forwards ? to : from;
    m_speed = speed * kSecondsPerFrame;
    m_direction = forwards ? 0 : -1;
    m_loopType = loopType;
    m_complete = false;
    m_anim = anim;
}

// 0x004813B0
void CAnimTimeline::GetTimes(float* start, float* current, float* end)
{
    *current = m_ongoing;
    if (m_direction == 0) {
        *start = m_outset;
        *end = m_finish;
    } else {
        *start = m_finish;
        *end = m_outset;
    }
}

// 0x004813F0
bool CAnimTimeline::IsAccomplished()
{
    if (m_loopType != kOnce)
        return false;
    return m_direction == 0 ? m_ongoing == m_finish : m_ongoing == m_outset;
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
        double next = FrameClock::GetSlomoFrameSeconds() * kFramesPerSecond * m_speed + m_ongoing;
        stepped = static_cast<float>(next);
        m_complete = next > m_finish;
        if (m_outset == m_finish) {
            bound = m_finish;
            time = m_finish;
        } else {
            // (wrapped round the end, as many times as it takes - forever on a degenerate range, as the original)
            time = static_cast<double>(stepped) - m_finish + m_outset;
            while (time > m_finish)
                time = time - m_finish + m_outset;
            bound = m_finish;
        }
    } else if (m_direction == -1) {
        double next = m_ongoing - FrameClock::GetSlomoFrameSeconds() * kFramesPerSecond * m_speed;
        stepped = static_cast<float>(next);
        m_complete = next < m_outset;
        if (m_outset == m_finish) {
            time = m_finish;
        } else {
            time = static_cast<double>(stepped) - m_outset + m_finish;
            while (time < m_outset)
                time = time - m_outset + m_finish;
        }
        bound = m_outset;
    }
    if (m_complete && m_loopType == kOnce) {
        time = bound;
    } else if (m_complete && m_loopType == kPingPong) {
        time = bound;
        m_direction = m_direction != 0 ? 0 : -1;
    } else if (!m_complete || m_loopType != kCycle) {
        time = stepped;
    }
    if (time < m_outset)
        time = m_outset;
    if (time > m_finish)
        time = m_finish;
    return time;
}

// 0x004816B0
void CAnimTimeline::AdvanceClock(float frames)
{
    float speed = m_speed;
    m_speed = 0.0f;
    m_ongoing = static_cast<float>(static_cast<double>(frames) + m_ongoing);
    m_ongoing = static_cast<float>(NextTime());
    m_speed = speed;
}

// --- the blend timeline ---

// 0x00481830
CAnimTrack* CAnimTrack::MakeNew(GameObjects::CGameEntity* object)
{
    CAnimTimeline::MakeNew();
    m_controllerCount = 0;
    mp_events = nullptr;
    m_eventCount = 0;
    m_ownerObject = object;
    m_vtable = kBlendTimelineVtable;
    m_eventsOn = true;
    m_curPhase = kPlaying;
    return this;
}

// 0x00481960
void CAnimTrack::Reset()
{
    CAnimTimeline::Reset();
    m_curPhase = kOff;
    m_fadeLeft = 0.0f;
    m_fadeScale = 0.0f;
}

// 0x00481870
void CAnimTrack::DeleteControllers()
{
    for (int i = 0; i < m_controllerCount; ++i) {
        if (m_controllerList[i])
            DeleteController(m_controllerList[i]);
        m_controllerList[i] = nullptr;
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
        if (m_controllerList[i] == controller)
            break;
    }
    if (m_controllerList[i])
        DeleteController(m_controllerList[i]);
    m_controllerList[i] = nullptr;
    for (; i < m_controllerCount - 1; ++i)
        m_controllerList[i] = m_controllerList[i + 1];
    --m_controllerCount;
}

// 0x00481920
bool CAnimTrack::GetPoses(void* pose)
{
    for (int i = 0; i < m_controllerCount; ++i) {
        CAnimController* controller = m_controllerList[i];
        reinterpret_cast<bool(__thiscall*)(CAnimController*, void*)>(controller->m_vtable[1])(controller, pose);
    }
    return true;
}

// 0x00481980
bool CAnimTrack::IsOperative()
{
    return m_curPhase != kOff;
}

// 0x00481990
bool CAnimTrack::IsFading()
{
    return m_curPhase == kFading;
}

// 0x004819A0
void CAnimTrack::ArmEvents()
{
    for (uint32_t i = 0; i < m_eventCount; ++i)
        reinterpret_cast<CUserClipKey*>(&mp_events[i])->ChangeLive(true);
}

// 0x004819D0
GameObjects::CGameEntity* CAnimTrack::FetchSubject()
{
    return m_ownerObject;
}

// --- the base controller ---

// 0x00481710
CAnimController* CAnimController::MakeNew(CAnimTimeline* timeline)
{
    mp_timeline = timeline;
    m_vtable = kControllerVtable;
    m_ident = 0;
    m_ownerObject = nullptr;
    m_rank = 0;
    return this;
}

// 0x00481730
void CAnimController::DestroyBody()
{
    m_vtable = kControllerVtable;
}

// 0x004817A0
CAnimController* CAnimController::Teardown(uint8_t flags)
{
    m_vtable = kControllerVtable;
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x00481740
bool CAnimController::ConfigureFromData(Scripts::CStruct* params)
{
    return params->ReadHashValue(kId, &m_ident, 0);
}

// 0x00481760
GameObjects::CGameEntity* CAnimController::FetchSubject()
{
    return static_cast<CAnimTrack*>(mp_timeline)->FetchSubject();
}

// 0x00481770
uint8_t* CAnimController::FetchRig()
{
    GameObjects::CObjectModule* component = static_cast<CAnimTrack*>(mp_timeline)->FetchSubject()->FetchModule(kSkeleton);
    return component ? static_cast<GameObjects::CBoneRigModule*>(component)->m_boneRig : nullptr;
}

// --- the base event ---

// 0x004819E0
void CAnimEventBase::DestroyBody()
{
    m_vtable = kEventVtable;
}

namespace {
static VtableSlot kTimelineVtable_slots[] = {&Gfx2D::CAnimTimeline::Teardown, &Gfx2D::CAnimTimeline::Reset,
                &Gfx2D::CAnimTimeline::TickFrame, &Gfx2D::CAnimTimeline::PlayAnim, Stubs::IgnoreArgument};
void** const kTimelineVtable = reinterpret_cast<void**>(kTimelineVtable_slots);
static VtableSlot kControllerVtable_slots[] = {&Gfx2D::CAnimController::Teardown, Stubs::ReturnFalse1,
                Stubs::IgnoreArgument, &GameObjects::ComponentWithoutMemberFunctions::InvokeMethodByName,
                &Gfx2D::CAnimController::ConfigureFromData, Stubs::DoNothing};
void** const kControllerVtable = reinterpret_cast<void**>(kControllerVtable_slots);
} // namespace

} // namespace Gfx
