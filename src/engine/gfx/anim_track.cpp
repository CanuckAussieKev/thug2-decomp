// An animation playing on a blend channel - its controllers, events and fade: see anim_track.h.

#include "engine/gfx/anim_track.h"
#include "engine/gfx/bone_look_at.h"
#include "engine/gfx/custom_anim_key.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/slab_bank.h"
#include "engine/objects/anim_blender.h"
#include "engine/objects/flip_rotate.h"
#include "engine/objects/entity.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "engine/core/code_address.h"
#include "engine/core/game_vtables.h"
#include "engine/core/stubs.h"

namespace Gfx2D {

namespace {

using Scripts::CStruct;

// Member function and parameter names (checksums; those not recovered are only numbers).
constexpr uint32_t kAddAnimController = 0x83654874, kRemoveAnimController = 0x986D274E, kNewAnim = 0xC68365EB;
constexpr uint32_t kPlayAnim = 0xAF2FAE19, kSetTarget = 0xD0209498;
constexpr uint32_t kType = 0x7321A8D6, kPriority = 0x9D5923D8, kId = 0x40C698AF, kAnim = 0x98549BA4;
constexpr uint32_t kDuration = 0x79A07F3F, kSync = 0xD3D40A85, kFrame = 0x4A07C332, kTime = 0x906B67BA;
constexpr uint32_t kEvent = 0xC451F558, kParams = 0x7031F10C, kFlipped = 0x0C7A712C;
constexpr uint32_t kAnimName = 0x6C2BFB7F, kStartTime = 0xD16B61E6, kEndTime = 0xAB81BB3D, kRepeatMode = 0xCD8D7C1B;
constexpr uint32_t kBlendPeriod = 0x8F0D24ED, kSpeed = 0xF0D90109, kWeightedAnim = 0xD697A799;
constexpr uint32_t kWeight = 0xC6AE16AC, kFraction = 0x4D747FA0, kWeightedFlag = 0x1B92121C;
constexpr uint32_t kControllerParams = 0xAEEEC12E;
// Controller types.
constexpr uint32_t kWeightedController = 0x7E1940A1, kProceduralController = 0x26205DB6;
constexpr uint32_t kPartialController = 0x659BF355, kShakeController = 0x6D941203;
constexpr uint32_t kStandardController = 0xCB4533FF, kStaticPoseCtl = 0xCFC5B380;
constexpr uint32_t kTumbleController = 0xDF5C091A;
constexpr uint32_t kAnimBlender = 0x7149B753;
constexpr int kLoopWobble = 3;
enum : int { kNotMine = 2 };

void** const& kEventVtable = Vt::kVtblAnimCue; void** const& kEventBaseVtable = Vt::kAnimEventBase; void** const& kTrackVtable = Vt::kAnimTrack;

template <typename R, typename... A>
R Virtual(void* object, int slot, A... args)
{
    void** vtable = *static_cast<void***>(object);
    return reinterpret_cast<R(__thiscall*)(void*, A...)>(vtable[slot])(object, args...);
}

// The original's _ftol (0x006236FC): truncated towards zero.
int Truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// --- calls into code not rewritten here ---
// An event's set-up, arming, firing and destructor body.
void SetUpEvent(CTimedClipEvent* e, int frame, uint32_t type, CStruct* params)
{
    reinterpret_cast<CAnimScriptKey*>(e)->AssignValue(frame, type, params);
}
void ArmEvent(CTimedClipEvent* e, bool armed) { reinterpret_cast<CUserClipKey*>(e)->ChangeLive(armed); }
bool FireEvent(CTimedClipEvent* e, GameObjects::CGameEntity* object)
{
    return reinterpret_cast<CUserClipKey*>(e)->ProcessIfActive(object);
}
void DestroyEventBody(CTimedClipEvent* e) { reinterpret_cast<CAnimScriptKey*>(e)->DestroyBody(); }
// The 0x26205DB6 controller (0x1A4 bytes).
CAnimDriver* ConstructProceduralController(void* memory, CAnimTimeline* timeline)
{
    return reinterpret_cast<CAnimDriver*>(static_cast<CBoneLookAt*>(memory)->MakeNew(timeline, nullptr));
}

// A script structure from the structures' pools, and back.
CStruct* NewParams()
{
    CStruct* params = Scripts::SpawnParamBag();
    return params ? params->PrepareAll() : nullptr;
}
void DeleteParams(CStruct* params)
{
    if (params) {
        params->Clear();
        Scripts::DeleteStruct(params);
    }
}

} // namespace

// --- the events ---------------------------------------------------------------------------------------------

// 0x004819F0
CAnimEventBase* CAnimEventBase::Teardown(uint8_t flags)
{
    m_vtable = kEventBaseVtable;
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00481A20
CTimedClipEvent* CTimedClipEvent::MakeNew()
{
    m_vtable = kEventVtable;
    m_callArgs = nullptr;
    return this;
}

// 0x00481DB0
CTimedClipEvent* CTimedClipEvent::Teardown(uint8_t flags)
{
    if (flags & 2) {
        // (the compiler's array-destructor helper, 0x0062382B: last to first)
        uint32_t* block = reinterpret_cast<uint32_t*>(this) - 1;
        for (uint32_t i = *block; i > 0; --i)
            DestroyEventBody(this + (i - 1));
        if (flags & 1)
            MemSys::Hub::Pull()->Dealloc(block);
        return reinterpret_cast<CTimedClipEvent*>(block);
    }
    DestroyEventBody(this);
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// --- the track ----------------------------------------------------------------------------------------------

MemSys::PoolPair g_animTrackPools;

MemSys::PoolPair& CAnimTrack::Pools() { return g_animTrackPools; }

// 0x00481EE0
void* CAnimTrack::operator new(size_t size) { return Pools().Carve(static_cast<unsigned int>(size)); }

// 0x00482360
void CAnimTrack::operator delete(void* item) { Pools().Dealloc(item); }

// 0x00481F20
void CAnimTrack::DestroyBody()
{
    m_vtable = kTrackVtable;
    DeleteControllers();
    DeleteEvents();
    CAnimTimeline::DestroyBody();
}

// 0x00482340
CAnimTrack* CAnimTrack::Teardown(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        operator delete(this);
    return this;
}

// 0x00481A30
void CAnimTrack::InsertController(CAnimDriver* controller, int priority)
{
    // (no room check: six at most by the callers)
    int count = m_controllerCount;
    int at = 0;
    if (count > 0) {
        while (at < m_controllerCount && priority <= m_controllerList[at]->m_rank)
            ++at;
        if (at < m_controllerCount) {
            for (int i = count; i > at; --i)
                m_controllerList[i] = m_controllerList[i - 1];
        }
    }
    m_controllerList[at] = controller;
    ++m_controllerCount;
}

// 0x00481A80
CAnimDriver* CAnimTrack::FindController(uint32_t id)
{
    for (int i = 0; i < m_controllerCount; ++i) {
        if (m_controllerList[i]->m_ident == id)
            return m_controllerList[i];
    }
    return nullptr;
}

// 0x00481AC0
void CAnimTrack::TickFrame()
{
    m_complete = false;
    if (m_curPhase == kFading) {
        // (x87 at 53 bits: the frame's share of the rate, taken off the weight left; the comparisons use the
        // unrounded result)
        double step = FrameClock::GetSlomoFrameSeconds() * 60.0;
        step *= m_fadeRate;
        double left = static_cast<double>(m_fadeLeft) - step;
        m_fadeLeft = static_cast<float>(left);
        if (left <= 0.0 || left * m_fadeScale < 0.0)
            m_curPhase = kOff;
    }
    if (m_curPhase == kPlaying || m_curPhase == kFading) {
        for (int i = 0; i < m_controllerCount; ++i)
            Virtual<void>(m_controllerList[i], 5);
    }
}

// 0x00481B40
bool CAnimTrack::StartFade(float period)
{
    if (period == 0.0f)
        m_curPhase = kOff;
    if (m_curPhase != kPlaying)
        return false;
    m_curPhase = kFading;
    m_fadeLeft = period;
    return true;
}

// 0x00481B80
double CAnimTrack::GetWeight()
{
    switch (m_curPhase) {
    case kFading:
        return static_cast<double>(m_fadeScale) * m_fadeLeft;
    case kPlaying:
        return 1.0;
    default:
        return 0.0;
    }
}

// 0x00481BB0
void CAnimTrack::DeleteEvents()
{
    if (!m_eventCount || !m_eventsPtr)
        return;
    m_eventCount = 0;
    uint32_t* block = reinterpret_cast<uint32_t*>(m_eventsPtr) - 1;
    if (*block)
        Virtual<CTimedClipEvent*>(m_eventsPtr, 0, 3); // (the vector deleting destructor)
    else
        MemSys::Hub::Pull()->Dealloc(block);
    m_eventsPtr = nullptr;
}

// 0x00481C00
void CAnimTrack::LoadEvents(uint32_t anim)
{
    GameObjects::CObjectModule* component = m_ownerObject->FetchModule(kAnimBlender);
    if (!component)
        return;
    uint32_t tableName = static_cast<GameObjects::CAnimBlenderComponent*>(component)->GetFirstChannel58();
    if (!tableName)
        return;
    CStruct* table = Scripts::ReadSubStruct(tableName);
    if (!table)
        return;
    Scripts::CArray* list;
    if (!table->FetchList(anim, &list, 0))
        return;
    uint32_t count = list->m_extent;
    m_eventCount = count;
    if (!count)
        return;
    auto* block = static_cast<uint32_t*>(MemSys::Alloc(count * sizeof(CTimedClipEvent) + 4));
    CTimedClipEvent* events = nullptr;
    if (block) {
        *block = count;
        events = reinterpret_cast<CTimedClipEvent*>(block + 1);
        // (the compiler's array-constructor helper, 0x0062388B)
        for (uint32_t i = 0; i < count; ++i)
            events[i].MakeNew();
    }
    m_eventsPtr = events;
    for (uint32_t i = 0; i < m_eventCount; ++i) {
        auto* entry = reinterpret_cast<CStruct*>(list->FetchPart(i));
        int frame = 0;
        if (!entry->FetchIntValue(kFrame, &frame, 0)) {
            float seconds = 0.0f;
            entry->FetchFloat(kTime, &seconds, 1);
            frame = Truncated(static_cast<double>(seconds) * 60.0);
        }
        uint32_t type = 0; // (the original's is left as it was found on the stack when missing - an assert)
        entry->ReadHashValue(kEvent, &type, 1);
        CStruct* params = nullptr;
        entry->ReadSubStruct(kParams, &params, 0);
        SetUpEvent(&m_eventsPtr[i], frame, type, params);
        ArmEvent(&m_eventsPtr[i], true);
    }
}

// 0x00481E10
void CAnimTrack::FireEvents(float from, float to)
{
    if (!m_eventsOn || m_curPhase != kPlaying)
        return;
    // (frames: compared unrounded, passed on as floats)
    double first = static_cast<double>(from) * 60.0;
    double last = static_cast<double>(to) * 60.0;
    float firstFrame = static_cast<float>(first);
    float lastFrame = static_cast<float>(last);
    bool looped = m_direction == 0 ? first > last : last > first;
    if (looped) {
        ArmEvents();
        firstFrame = m_outset; // (as the original: the start itself, not in frames)
    }
    for (uint32_t i = 0; i < m_eventCount; ++i) {
        ArmEvent(&m_eventsPtr[i], true);
        if (Virtual<bool>(&m_eventsPtr[i], 1, firstFrame, lastFrame, 0))
            FireEvent(&m_eventsPtr[i], m_ownerObject);
    }
}

// 0x00481FB0
CAnimDriver* CAnimTrack::AttachController(CStruct* params)
{
    uint32_t type;
    params->ReadHashValue(kType, &type, 1);
    int priority = 500;
    CAnimDriver* controller;
    void* memory;
    switch (type) {
    case kWeightedController:
        memory = MemSys::AllocZeroed(sizeof(CWeightedAnimController));
        controller = memory ? static_cast<CWeightedAnimController*>(memory)->MakeNew(this, nullptr) : nullptr;
        break;
    case kProceduralController:
        memory = MemSys::AllocZeroed(0x1A4);
        controller = memory ? ConstructProceduralController(memory, this) : nullptr;
        break;
    case kPartialController:
        memory = MemSys::AllocZeroed(sizeof(CPartialClipDriver));
        controller = memory ? static_cast<CPartialClipDriver*>(memory)->MakeNew(this) : nullptr;
        break;
    case kShakeController:
        memory = MemSys::AllocZeroed(sizeof(CWobbleDriver));
        controller = memory ? static_cast<CWobbleDriver*>(memory)->MakeNew(this) : nullptr;
        break;
    case kStandardController:
        memory = CStandardAnimController::operator new(sizeof(CStandardAnimController), 0);
        controller = memory ? static_cast<CStandardAnimController*>(memory)->MakeNew(this) : nullptr;
        break;
    case kStaticPoseCtl:
        memory = MemSys::AllocZeroed(sizeof(CStanceDriver));
        controller = memory ? static_cast<CStanceDriver*>(memory)->MakeNew(this) : nullptr;
        break;
    case kTumbleController:
        memory = MemSys::AllocZeroed(sizeof(GameObjects::CFlipRotateComponent));
        controller = memory ? reinterpret_cast<CAnimDriver*>(
                                  static_cast<GameObjects::CFlipRotateComponent*>(memory)->MakeNew(this))
                            : nullptr;
        priority = 100;
        break;
    default:
        return nullptr;
    }
    params->FetchIntValue(kPriority, &priority, 0);
    // (a failed allocation goes on with null: the call below crashes, as in the original)
    Virtual<void>(controller, 4, params);
    controller->m_rank = priority;
    InsertController(controller, priority);
    return controller;
}

// 0x00482200
int CAnimTrack::InvokeMethodByName(uint32_t name, CStruct* params, Scripts::CScript* script)
{
    if (name == kAddAnimController) {
        AttachController(params);
        return 1;
    }
    if (name == kRemoveAnimController) {
        uint32_t id = reinterpret_cast<uint32_t>(params); // (the argument's own slot: the params' address if missing)
        params->ReadHashValue(kId, &id, 1);
        if (CAnimDriver* controller = FindController(id))
            DetachAnimDriver(controller);
    } else if (name == kNewAnim) {
        uint32_t anim = 0; // (the original's is left as it was found on the stack when missing - an assert)
        params->ReadHashValue(kAnim, &anim, 1);
        float duration;
        std::memcpy(&duration, &params, sizeof(duration)); // (the argument's own slot, as above)
        params->FetchFloat(kDuration, &duration, 1);
        bool sync = params->HasBareFlag(kSync);
        m_anim = anim;
        DeleteEvents();
        LoadEvents(m_anim);
        float current = 0.0f;
        if (sync) {
            // (x87 at 53 bits: the place on the old timeline in proportion, rounded once)
            double place = static_cast<double>(m_ongoing);
            m_ongoing = 0.0f;
            current = static_cast<float>(place / m_finish * duration);
        }
        m_finish = duration;
        m_outset = 0.0f;
        AdvanceClock(current);
    }
    for (int i = 0; i < m_controllerCount; ++i) {
        int result = Virtual<int>(m_controllerList[i], 3, name, params, script);
        if (result != kNotMine)
            return result;
    }
    return kNotMine;
}

// 0x004823C0
void CAnimTrack::PlayAnim(uint32_t anim, float start, float end, int loopType, float blendPeriod, float speed,
                          bool flipped, CStruct* params)
{
    DeleteEvents();
    LoadEvents(anim);
    m_curPhase = kPlaying;
    m_fadeRate = 1.0f / 60.0f; // (0x3C888889)

    CStruct* settings = NewParams();
    settings->PutNameKey(kType, loopType == kLoopWobble ? kShakeController : kStandardController);
    AttachController(settings);
    DeleteParams(settings);

    uint32_t weightedAnim = anim;
    float weight = 0.0f;
    if (params) {
        if (params->ReadHashValue(kWeightedAnim, &weightedAnim, 0) || params->HasBareFlag(kWeightedFlag)) {
            params->FetchFloat(kWeight, &weight, 0);
            settings = NewParams();
            settings->PutNameKey(kType, kWeightedController);
            AttachController(settings);
            DeleteParams(settings);
        }
        CStruct* controllerParams = nullptr;
        if (params->ReadSubStruct(kControllerParams, &controllerParams, 0)) {
            settings = NewParams();
            settings->PutNameKey(kType, kProceduralController);
            settings->PutSubStruct(0u, controllerParams); // (no name)
            AttachController(settings);
            DeleteParams(settings);
        }
    }
    settings = NewParams();
    settings->PutNameKey(kType, kTumbleController);
    settings->PutInt(kFlipped, flipped);
    AttachController(settings);
    DeleteParams(settings);

    settings = NewParams();
    settings->PushFloat(kFraction, 0.0f);
    for (int i = 0; i < m_controllerCount; ++i)
        Virtual<int>(m_controllerList[i], 3, kSetTarget, settings, static_cast<Scripts::CScript*>(nullptr));
    DeleteParams(settings);

    settings = NewParams();
    settings->PutNameKey(kAnimName, anim);
    settings->PushFloat(kStartTime, start);
    settings->PushFloat(kEndTime, end);
    settings->PutInt(kRepeatMode, loopType);
    settings->PushFloat(kBlendPeriod, blendPeriod);
    settings->PushFloat(kSpeed, speed);
    settings->PutNameKey(kWeightedAnim, weightedAnim);
    settings->PushFloat(kWeight, weight);
    for (int i = 0; i < m_controllerCount; ++i)
        Virtual<int>(m_controllerList[i], 3, kPlayAnim, settings, static_cast<Scripts::CScript*>(nullptr));
    DeleteParams(settings);
}

} // namespace Gfx

namespace Vt {
static VtableSlot kAnimTrack_slots[] = {&Gfx2D::CAnimTrack::Teardown, &Gfx2D::CAnimTrack::Reset, &Gfx2D::CAnimTrack::TickFrame,
                &Gfx2D::CAnimTimeline::PlayAnim, Stubs::IgnoreArgument, &Gfx2D::CAnimTrack::InvokeMethodByName,
                &Gfx2D::CAnimTrack::PlayAnim};
void** const kAnimTrack = reinterpret_cast<void**>(kAnimTrack_slots);
static VtableSlot kAnimEventBase_slots[] = {&Gfx2D::CAnimEventBase::Teardown, &Gfx2D::CUserClipKey::InsideRange,
                &_purecall};
void** const kAnimEventBase = reinterpret_cast<void**>(kAnimEventBase_slots);
static VtableSlot kAnimEvent_slots[] = {&Gfx2D::CTimedClipEvent::Teardown, &Gfx2D::CUserClipKey::InsideRange,
                &Gfx2D::CAnimScriptKey::WorkOn};
void** const kVtblAnimCue = reinterpret_cast<void**>(kAnimEvent_slots);
} // namespace Vt
