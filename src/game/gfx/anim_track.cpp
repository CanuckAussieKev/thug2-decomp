// An animation playing on a blend channel - its controllers, events and fade: see anim_track.h.

#include "game/gfx/anim_track.h"

#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/obj/anim_blender.h"
#include "game/obj/flip_rotate.h"
#include "game/obj/object.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Gfx {

namespace {

using Script::CStruct;

// Member function and parameter names (checksums; those not recovered are only numbers).
constexpr uint32_t kAddAnimController = 0x83654874, kRemoveAnimController = 0x986D274E, kNewAnim = 0xC68365EB;
constexpr uint32_t kPlayAnim = 0xAF2FAE19, kSetTarget = 0xD0209498;
constexpr uint32_t kType = 0x7321A8D6, kPriority = 0x9D5923D8, kId = 0x40C698AF, kAnim = 0x98549BA4;
constexpr uint32_t kDuration = 0x79A07F3F, kSync = 0xD3D40A85, kFrame = 0x4A07C332, kTime = 0x906B67BA;
constexpr uint32_t kEvent = 0xC451F558, kParams = 0x7031F10C, kFlipped = 0x0C7A712C;
constexpr uint32_t kAnimName = 0x6C2BFB7F, kStartTime = 0xD16B61E6, kEndTime = 0xAB81BB3D, kLoopType = 0xCD8D7C1B;
constexpr uint32_t kBlendPeriod = 0x8F0D24ED, kSpeed = 0xF0D90109, kWeightedAnim = 0xD697A799;
constexpr uint32_t kWeight = 0xC6AE16AC, kFraction = 0x4D747FA0, kWeightedFlag = 0x1B92121C;
constexpr uint32_t kControllerParams = 0xAEEEC12E;
// Controller types.
constexpr uint32_t kWeightedController = 0x7E1940A1, kProceduralController = 0x26205DB6;
constexpr uint32_t kPartialController = 0x659BF355, kWobbleController = 0x6D941203;
constexpr uint32_t kStandardController = 0xCB4533FF, kPoseController = 0xCFC5B380;
constexpr uint32_t kFlipRotateController = 0xDF5C091A;
constexpr uint32_t kAnimBlender = 0x7149B753;
constexpr int kLoopWobble = 3;
enum : int { kNotMine = 2 };

constexpr uint32_t kEventVtable = 0x006487BC, kEventBaseVtable = 0x006487B0, kTrackVtable = 0x00648794;
void** Vtable(uint32_t address) { return reinterpret_cast<void**>(address); }

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
void SetUpEvent(CAnimEvent* e, int frame, uint32_t type, CStruct* params)
{
    GameFunction<void(__thiscall*)(CAnimEvent*, int, uint32_t, CStruct*)>(0x004865D0)(e, frame, type, params);
}
void ArmEvent(CAnimEvent* e, bool armed) { GameFunction<void(__thiscall*)(CAnimEvent*, bool)>(0x00486150)(e, armed); }
bool FireEvent(CAnimEvent* e, Obj::CCompositeObject* object)
{
    return GameFunction<bool(__thiscall*)(CAnimEvent*, Obj::CCompositeObject*)>(0x00486160)(e, object);
}
void DestroyEventBody(CAnimEvent* e) { GameFunction<void(__thiscall*)(CAnimEvent*)>(0x00486560)(e); }
// The 0x26205DB6 controller (0x1A4 bytes).
CAnimController* ConstructProceduralController(void* memory, CAnimTimeline* timeline)
{
    return GameFunction<CAnimController*(__thiscall*)(void*, CAnimTimeline*, int)>(0x00488300)(memory, timeline, 0);
}

// A script structure from the structures' pools, and back.
CStruct* NewParams()
{
    CStruct* params = Script::NewStruct();
    return params ? params->Init() : nullptr;
}
void DeleteParams(CStruct* params)
{
    if (params) {
        params->Clear();
        Script::DeleteStruct(params);
    }
}

} // namespace

// --- the events ---------------------------------------------------------------------------------------------

// 0x004819F0
CAnimEventBase* CAnimEventBase::Destroy(uint8_t flags)
{
    m_vtable = Vtable(kEventBaseVtable);
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00481A20
CAnimEvent* CAnimEvent::Construct()
{
    m_vtable = Vtable(kEventVtable);
    mp_params = nullptr;
    return this;
}

// 0x00481DB0
CAnimEvent* CAnimEvent::Destroy(uint8_t flags)
{
    if (flags & 2) {
        // (the compiler's array-destructor helper, 0x0062382B: last to first)
        uint32_t* block = reinterpret_cast<uint32_t*>(this) - 1;
        for (uint32_t i = *block; i > 0; --i)
            DestroyEventBody(this + (i - 1));
        if (flags & 1)
            Mem::Manager::Get()->Free(block);
        return reinterpret_cast<CAnimEvent*>(block);
    }
    DestroyEventBody(this);
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// --- the track ----------------------------------------------------------------------------------------------

Mem::PoolPair& CAnimTrack::Pools() { return GameVariable<Mem::PoolPair>(0x006F92C8); }

// 0x00481EE0
void* CAnimTrack::operator new(size_t size) { return Pools().Allocate(static_cast<unsigned int>(size)); }

// 0x00482360
void CAnimTrack::operator delete(void* item) { Pools().Free(item); }

// 0x00481F20
void CAnimTrack::DestroyBody()
{
    m_vtable = Vtable(kTrackVtable);
    DeleteControllers();
    DeleteEvents();
    CAnimTimeline::DestroyBody();
}

// 0x00482340
CAnimTrack* CAnimTrack::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        operator delete(this);
    return this;
}

// 0x00481A30
void CAnimTrack::InsertController(CAnimController* controller, int priority)
{
    // (no room check: six at most by the callers)
    int count = m_controllerCount;
    int at = 0;
    if (count > 0) {
        while (at < m_controllerCount && priority <= mp_controllers[at]->m_priority)
            ++at;
        if (at < m_controllerCount) {
            for (int i = count; i > at; --i)
                mp_controllers[i] = mp_controllers[i - 1];
        }
    }
    mp_controllers[at] = controller;
    ++m_controllerCount;
}

// 0x00481A80
CAnimController* CAnimTrack::FindController(uint32_t id)
{
    for (int i = 0; i < m_controllerCount; ++i) {
        if (mp_controllers[i]->m_id == id)
            return mp_controllers[i];
    }
    return nullptr;
}

// 0x00481AC0
void CAnimTrack::Update()
{
    m_complete = false;
    if (m_state == kFading) {
        // (x87 at 53 bits: the frame's share of the rate, taken off the weight left; the comparisons use the
        // unrounded result)
        double step = Tmr::GetSlomoFrameSeconds() * 60.0;
        step *= m_fadeRate;
        double left = static_cast<double>(m_fadeLeft) - step;
        m_fadeLeft = static_cast<float>(left);
        if (left <= 0.0 || left * m_fadeScale < 0.0)
            m_state = kOff;
    }
    if (m_state == kPlaying || m_state == kFading) {
        for (int i = 0; i < m_controllerCount; ++i)
            Virtual<void>(mp_controllers[i], 5);
    }
}

// 0x00481B40
bool CAnimTrack::StartFade(float period)
{
    if (period == 0.0f)
        m_state = kOff;
    if (m_state != kPlaying)
        return false;
    m_state = kFading;
    m_fadeLeft = period;
    return true;
}

// 0x00481B80
double CAnimTrack::GetWeight()
{
    switch (m_state) {
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
    if (!m_eventCount || !mp_events)
        return;
    m_eventCount = 0;
    uint32_t* block = reinterpret_cast<uint32_t*>(mp_events) - 1;
    if (*block)
        Virtual<CAnimEvent*>(mp_events, 0, 3); // (the vector deleting destructor)
    else
        Mem::Manager::Get()->Free(block);
    mp_events = nullptr;
}

// 0x00481C00
void CAnimTrack::LoadEvents(uint32_t anim)
{
    Obj::CBaseComponent* component = mp_object->GetComponent(kAnimBlender);
    if (!component)
        return;
    uint32_t tableName = static_cast<Obj::CAnimBlenderComponent*>(component)->GetFirstChannel58();
    if (!tableName)
        return;
    CStruct* table = Script::GetStructure(tableName);
    if (!table)
        return;
    Script::CArray* list;
    if (!table->GetArray(anim, &list, 0))
        return;
    uint32_t count = list->m_size;
    m_eventCount = count;
    if (!count)
        return;
    auto* block = static_cast<uint32_t*>(Mem::New(count * sizeof(CAnimEvent) + 4));
    CAnimEvent* events = nullptr;
    if (block) {
        *block = count;
        events = reinterpret_cast<CAnimEvent*>(block + 1);
        // (the compiler's array-constructor helper, 0x0062388B)
        for (uint32_t i = 0; i < count; ++i)
            events[i].Construct();
    }
    mp_events = events;
    for (uint32_t i = 0; i < m_eventCount; ++i) {
        auto* entry = reinterpret_cast<CStruct*>(list->GetElement(i));
        int frame = 0;
        if (!entry->GetInteger(kFrame, &frame, 0)) {
            float seconds = 0.0f;
            entry->GetFloat(kTime, &seconds, 1);
            frame = Truncated(static_cast<double>(seconds) * 60.0);
        }
        uint32_t type = 0; // (the original's is left as it was found on the stack when missing - an assert)
        entry->GetChecksum(kEvent, &type, 1);
        CStruct* params = nullptr;
        entry->GetStructure(kParams, &params, 0);
        SetUpEvent(&mp_events[i], frame, type, params);
        ArmEvent(&mp_events[i], true);
    }
}

// 0x00481E10
void CAnimTrack::FireEvents(float from, float to)
{
    if (!m_eventsOn || m_state != kPlaying)
        return;
    // (frames: compared unrounded, passed on as floats)
    double first = static_cast<double>(from) * 60.0;
    double last = static_cast<double>(to) * 60.0;
    float firstFrame = static_cast<float>(first);
    float lastFrame = static_cast<float>(last);
    bool looped = m_direction == 0 ? first > last : last > first;
    if (looped) {
        ArmEvents();
        firstFrame = m_start; // (as the original: the start itself, not in frames)
    }
    for (uint32_t i = 0; i < m_eventCount; ++i) {
        ArmEvent(&mp_events[i], true);
        if (Virtual<bool>(&mp_events[i], 1, firstFrame, lastFrame, 0))
            FireEvent(&mp_events[i], mp_object);
    }
}

// 0x00481FB0
CAnimController* CAnimTrack::AddController(CStruct* params)
{
    uint32_t type;
    params->GetChecksum(kType, &type, 1);
    int priority = 500;
    CAnimController* controller;
    void* memory;
    switch (type) {
    case kWeightedController:
        memory = Mem::Calloc(sizeof(CWeightedAnimController));
        controller = memory ? static_cast<CWeightedAnimController*>(memory)->Construct(this, nullptr) : nullptr;
        break;
    case kProceduralController:
        memory = Mem::Calloc(0x1A4);
        controller = memory ? ConstructProceduralController(memory, this) : nullptr;
        break;
    case kPartialController:
        memory = Mem::Calloc(sizeof(CPartialAnimController));
        controller = memory ? static_cast<CPartialAnimController*>(memory)->Construct(this) : nullptr;
        break;
    case kWobbleController:
        memory = Mem::Calloc(sizeof(CWobbleController));
        controller = memory ? static_cast<CWobbleController*>(memory)->Construct(this) : nullptr;
        break;
    case kStandardController:
        memory = CStandardAnimController::operator new(sizeof(CStandardAnimController), 0);
        controller = memory ? static_cast<CStandardAnimController*>(memory)->Construct(this) : nullptr;
        break;
    case kPoseController:
        memory = Mem::Calloc(sizeof(CPoseController));
        controller = memory ? static_cast<CPoseController*>(memory)->Construct(this) : nullptr;
        break;
    case kFlipRotateController:
        memory = Mem::Calloc(sizeof(Obj::CFlipRotateComponent));
        controller = memory ? reinterpret_cast<CAnimController*>(
                                  static_cast<Obj::CFlipRotateComponent*>(memory)->Construct(this))
                            : nullptr;
        priority = 100;
        break;
    default:
        return nullptr;
    }
    params->GetInteger(kPriority, &priority, 0);
    // (a failed allocation goes on with null: the call below crashes, as in the original)
    Virtual<void>(controller, 4, params);
    controller->m_priority = priority;
    InsertController(controller, priority);
    return controller;
}

// 0x00482200
int CAnimTrack::CallMemberFunction(uint32_t name, CStruct* params, Script::CScript* script)
{
    if (name == kAddAnimController) {
        AddController(params);
        return 1;
    }
    if (name == kRemoveAnimController) {
        uint32_t id = reinterpret_cast<uint32_t>(params); // (the argument's own slot: the params' address if missing)
        params->GetChecksum(kId, &id, 1);
        if (CAnimController* controller = FindController(id))
            RemoveController(controller);
    } else if (name == kNewAnim) {
        uint32_t anim = 0; // (the original's is left as it was found on the stack when missing - an assert)
        params->GetChecksum(kAnim, &anim, 1);
        float duration;
        std::memcpy(&duration, &params, sizeof(duration)); // (the argument's own slot, as above)
        params->GetFloat(kDuration, &duration, 1);
        bool sync = params->ContainsFlag(kSync);
        m_anim = anim;
        DeleteEvents();
        LoadEvents(m_anim);
        float current = 0.0f;
        if (sync) {
            // (x87 at 53 bits: the place on the old timeline in proportion, rounded once)
            double place = static_cast<double>(m_current);
            m_current = 0.0f;
            current = static_cast<float>(place / m_end * duration);
        }
        m_end = duration;
        m_start = 0.0f;
        AddTime(current);
    }
    for (int i = 0; i < m_controllerCount; ++i) {
        int result = Virtual<int>(mp_controllers[i], 3, name, params, script);
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
    m_state = kPlaying;
    m_fadeRate = 1.0f / 60.0f; // (0x3C888889)

    CStruct* settings = NewParams();
    settings->AddChecksum(kType, loopType == kLoopWobble ? kWobbleController : kStandardController);
    AddController(settings);
    DeleteParams(settings);

    uint32_t weightedAnim = anim;
    float weight = 0.0f;
    if (params) {
        if (params->GetChecksum(kWeightedAnim, &weightedAnim, 0) || params->ContainsFlag(kWeightedFlag)) {
            params->GetFloat(kWeight, &weight, 0);
            settings = NewParams();
            settings->AddChecksum(kType, kWeightedController);
            AddController(settings);
            DeleteParams(settings);
        }
        CStruct* controllerParams = nullptr;
        if (params->GetStructure(kControllerParams, &controllerParams, 0)) {
            settings = NewParams();
            settings->AddChecksum(kType, kProceduralController);
            settings->AddStructure(0u, controllerParams); // (no name)
            AddController(settings);
            DeleteParams(settings);
        }
    }
    settings = NewParams();
    settings->AddChecksum(kType, kFlipRotateController);
    settings->AddInteger(kFlipped, flipped);
    AddController(settings);
    DeleteParams(settings);

    settings = NewParams();
    settings->AddFloat(kFraction, 0.0f);
    for (int i = 0; i < m_controllerCount; ++i)
        Virtual<int>(mp_controllers[i], 3, kSetTarget, settings, static_cast<Script::CScript*>(nullptr));
    DeleteParams(settings);

    settings = NewParams();
    settings->AddChecksum(kAnimName, anim);
    settings->AddFloat(kStartTime, start);
    settings->AddFloat(kEndTime, end);
    settings->AddInteger(kLoopType, loopType);
    settings->AddFloat(kBlendPeriod, blendPeriod);
    settings->AddFloat(kSpeed, speed);
    settings->AddChecksum(kWeightedAnim, weightedAnim);
    settings->AddFloat(kWeight, weight);
    for (int i = 0; i < m_controllerCount; ++i)
        Virtual<int>(mp_controllers[i], 3, kPlayAnim, settings, static_cast<Script::CScript*>(nullptr));
    DeleteParams(settings);
}

} // namespace Gfx

REPLACE(0x004819F0, &Gfx::CAnimEventBase::Destroy);
REPLACE(0x00481A20, &Gfx::CAnimEvent::Construct);
REPLACE(0x00481DB0, &Gfx::CAnimEvent::Destroy);
REPLACE(0x00481EE0, static_cast<void* (*)(size_t)>(&Gfx::CAnimTrack::operator new));
REPLACE(0x00482360, static_cast<void (*)(void*)>(&Gfx::CAnimTrack::operator delete));
REPLACE(0x00481F20, &Gfx::CAnimTrack::DestroyBody);
REPLACE(0x00482340, &Gfx::CAnimTrack::Destroy);
REPLACE(0x00481A30, &Gfx::CAnimTrack::InsertController);
REPLACE(0x00481A80, &Gfx::CAnimTrack::FindController);
REPLACE(0x00481AC0, &Gfx::CAnimTrack::Update);
REPLACE(0x00481B40, &Gfx::CAnimTrack::StartFade);
REPLACE(0x00481B80, &Gfx::CAnimTrack::GetWeight);
REPLACE(0x00481BB0, &Gfx::CAnimTrack::DeleteEvents);
REPLACE(0x00481C00, &Gfx::CAnimTrack::LoadEvents);
REPLACE(0x00481E10, &Gfx::CAnimTrack::FireEvents);
REPLACE(0x00481FB0, &Gfx::CAnimTrack::AddController);
REPLACE(0x00482200, &Gfx::CAnimTrack::CallMemberFunction);
REPLACE(0x004823C0, &Gfx::CAnimTrack::PlayAnim);
