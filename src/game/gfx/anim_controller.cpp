// The animation controllers (standard, weighted, wobble, pose, partial animation): see anim_controller.h.

#include "game/gfx/anim_controller.h"

#include <cmath>
#include <cstring>

#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/obj/anim_blender.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/script/names.h"
#include "game/sys/game_clock.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"

namespace Gfx {

namespace {

using Script::CStruct;

// Member function and parameter names (checksums; those not recovered are only numbers).
constexpr uint32_t kPlayAnim = 0xAF2FAE19, kNewSource = 0xC68365EB, kInvalidateCache = 0x5F495AE0;
constexpr uint32_t kGlobalFrame = 0x92267217, kSetWeightedAnim = 0x5E3F0F17;
constexpr uint32_t kSetTarget = 0xD0209498, kSetWobbleDetails = 0xEA6D0EFD;
constexpr uint32_t kStep = 0x6AAEB76F, kSetSpeed = 0xBD4EDD44, kGetPartialAnimParams = 0xD63A1B81;
constexpr uint32_t kReverse = 0xF5E2B871;
constexpr uint32_t kAnimName = 0x6C2BFB7F, kStartTime = 0xD16B61E6, kEndTime = 0xAB81BB3D, kLoopType = 0xCD8D7C1B;
constexpr uint32_t kBlendPeriod = 0x8F0D24ED, kSpeed = 0xF0D90109, kAnim = 0x98549BA4, kCurrent = 0x230CCBF4;
constexpr uint32_t kWeightedAnim = 0xD697A799, kWeight = 0xC6AE16AC, kFraction = 0x4D747FA0;
constexpr uint32_t kWobbleAmpA = 0xFD266A26, kWobbleAmpB = 0x642F3B9C, kWobbleK1 = 0x0F43FD49;
constexpr uint32_t kWobbleK2 = 0x964AACF3, kSpazFactor = 0xF90B0824, kFrames = 0x06C9F278;
constexpr uint32_t kPartialAnimTime = 0xAAECB346, kPartialAnim = 0x659BF355, kPartialAnimComplete = 0x4966CC11;
// Component types.
constexpr uint32_t kAnimBlender = 0x7149B753, kSkeleton = 0x222756D5;

enum : int { kNotMine = 2 };

float Constant(uint32_t address) { return GameVariable<float>(address); }
float FramesPerSecond() { return Constant(0x00645EA0); } // 60
float SecondsPerFrame() { return Constant(0x006463B8); } // 1/60
float TwoPi() { return Constant(0x0064545C); }

// The original's _ftol (0x006236FC): truncated towards zero.
int Truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

double X87Sin(double angle)
{
    double result;
    __asm {
        fld angle
        fsin
        fstp result
    }
    return result;
}

void** Vtable(uint32_t address) { return reinterpret_cast<void**>(address); }

// The base controller.
void BaseConstruct(CAnimController* c, CAnimTimeline* timeline) { c->CAnimController::Construct(timeline); }
void BaseDestroyBody(CAnimController* c) { c->CAnimController::DestroyBody(); }
void BaseInit(CAnimController* c, CStruct* params) { c->CAnimController::InitFromStructure(params); }
// The object (the blend timeline's +0x64) and its skeleton (the Skeleton component's +0x18, or null).
Obj::CCompositeObject* ObjectOf(CAnimController* c) { return c->CAnimController::GetObject(); }
uint8_t* SkeletonOf(CAnimController* c) { return c->CAnimController::GetSkeleton(); }
// The object's anim set (the animation blender component's 0x00419AB0) and an animation's length in it.
uint32_t AnimSet(Obj::CCompositeObject* object)
{
    Obj::CBaseComponent* blender = object->GetComponent(kAnimBlender);
    return static_cast<Obj::CAnimBlenderComponent*>(blender)->GetAnimSet();
}

// The timeline's pieces (thiscall, not rewritten yet).
float Step(CAnimTimeline* t) { return GameFunction<float(__thiscall*)(CAnimTimeline*)>(0x004814B0)(t); }
void PassedEvents(CAnimTimeline* t, float from, float to)
{
    GameFunction<void(__thiscall*)(CAnimTimeline*, float, float)>(0x00481E10)(t, from, to);
}
void PlayAnim(CAnimTimeline* t, uint32_t anim, float start, float end, int loopType, float blend, float speed)
{
    GameFunction<void(__thiscall*)(CAnimTimeline*, uint32_t, float, float, int, float, float)>(0x00481430)(
        t, anim, start, end, loopType, blend, speed);
}
bool IsComplete(CAnimTimeline* t) { return GameFunction<bool(__thiscall*)(CAnimTimeline*)>(0x004813F0)(t); }

// Frame sources (0x0048BFC0 makes one, 0x0048BFD0 frees it) and their frames.
void* NewSource() { return GameFunction<void*(__cdecl*)()>(0x0048BFC0)(); }
void FreeSource(void* source) { GameFunction<void(__cdecl*)(void*)>(0x0048BFD0)(source); }
void* SourceFrame(void* source, bool done) { return GameFunction<void*(__thiscall*)(void*, bool)>(0x0048CFA0)(source, done); }
void* SourceFrameOf(void* source, uint32_t anim)
{
    return GameFunction<void*(__thiscall*)(void*, uint32_t)>(0x0048CFD0)(source, anim);
}
int FrameIndex(void* frame) { return GameFunction<int(__thiscall*)(void*)>(0x00482BA0)(frame); }
// NOT ORIGINAL - the frame of an animation the source's set hasn't is null, and FrameIndex reads through it (a crash at
// 0x00482BA2, as in the original: e.g. peds whose animations weren't loaded, or a converted level's script playing one
// the skeleton hasn't): logged first, with the animation's name.
void* LoggedSourceFrameOf(void* source, uint32_t set, uint32_t anim)
{
    void* frame = SourceFrameOf(source, set + anim);
    if (!frame) {
        const char* name = Script::FindChecksumName(anim);
        Log("anim: no animation 0x%08X%s%s%s in the set 0x%08X - the next step (FrameIndex 0x00482BA0) crashes", anim,
            name ? " (" : "", name ? name : "", name ? ")" : "", set);
    }
    return frame;
}
void ReleaseFrameIndex(void* frame, int index) { GameFunction<void(__thiscall*)(void*, int)>(0x00482BD0)(frame, index); }
void FramePose(void* frame, uint8_t* pose, float time, int index, void* source)
{
    GameFunction<bool(__thiscall*)(void*, uint8_t*, uint8_t*, uint8_t*, float, int, void*)>(0x00485730)(
        frame, pose, pose + 0x400, pose + 0x800, time, index, source);
}
// The frame source told where the skeleton's bones are (its +0x20C, from the skeleton's +0x08) and how many
// (+0x210, from the skeleton's +0x0C).
void SetBones(void* source, const uint8_t* skeleton)
{
    auto* bones = *reinterpret_cast<uint8_t* const*>(skeleton + 8);
    *reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(source) + 0x20C) = bones ? bones + 0x408 : nullptr;
}
void SetBoneCount(void* source, const uint8_t* skeleton)
{
    *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(source) + 0x210) =
        *reinterpret_cast<const uint32_t*>(skeleton + 0xC);
}

// The game clock (ms) plus its offset.
int Clock() { return Tmr::GameClockElapsed() + GameVariable<int>(0x00696D60); }

// The play-anim member function's parameters (the original leaves a missing one uninitialised: 0 here).
struct PlayParams {
    uint32_t anim;
    float start = 0.0f, end = 0.0f, blend = 0.0f, speed = 0.0f;
    int loopType = 0;
};
PlayParams ReadPlayParams(CStruct* params)
{
    // (the anim name goes in the argument's own slot: the params' address if missing)
    PlayParams p;
    p.anim = reinterpret_cast<uint32_t>(params);
    params->GetChecksum(kAnimName, &p.anim, 1);
    params->GetFloat(kStartTime, &p.start, 1);
    params->GetFloat(kEndTime, &p.end, 1);
    params->GetInteger(kLoopType, &p.loopType, 1);
    params->GetFloat(kBlendPeriod, &p.blend, 1);
    params->GetFloat(kSpeed, &p.speed, 1);
    return p;
}

Mem::PoolPair& StandardPools() { return GameVariable<Mem::PoolPair>(0x00701430); }

} // namespace

// 0x004A4610
void CAnimController::BaseInitFromStructure(Script::CStruct* params) { BaseInit(this, params); }

// --- the standard controller ---------------------------------------------------------------------------------

// 0x004A4F30
void* CStandardAnimController::operator new(size_t size, int)
{
    return StandardPools().Allocate(static_cast<unsigned int>(size));
}

// 0x004A5600
void CStandardAnimController::operator delete(void* item) { StandardPools().Free(item); }

// 0x004A4500
CStandardAnimController* CStandardAnimController::Construct(CAnimTimeline* timeline)
{
    BaseConstruct(this, timeline);
    m_vtable = Vtable(0x00649F50);
    mp_source = NewSource();
    mp_frameData = SourceFrame(mp_source, false);
    m_animSet = AnimSet(ObjectOf(this));
    mp_frameData = nullptr;
    m_frameIndex = -1;
    return this;
}

// 0x004A4590
void CStandardAnimController::DestroyBody()
{
    m_vtable = Vtable(0x00649F50);
    if (mp_frameData)
        ReleaseFrameIndex(mp_frameData, m_frameIndex);
    if (mp_source) {
        FreeSource(mp_source);
        mp_source = nullptr;
    }
    mp_frameData = nullptr;
    BaseDestroyBody(this);
}

// 0x004A55E0
CStandardAnimController* CStandardAnimController::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        operator delete(this);
    return this;
}

// 0x004A4F70
bool CStandardAnimController::GetPose(uint8_t* pose)
{
    if (m_frameIndex == -1) {
        if (*reinterpret_cast<int*>(static_cast<uint8_t*>(mp_frameData) + 0x24))
            m_frameIndex = FrameIndex(mp_frameData);
        else
            mp_frameData = SourceFrame(mp_source, false);
    }
    SetBones(mp_source, SkeletonOf(this));
    SetBoneCount(mp_source, SkeletonOf(this));
    FramePose(mp_frameData, pose, mp_timeline->m_current, m_frameIndex, mp_source);
    SourceFrame(mp_source, true);
    return true;
}

// 0x004A4670
int CStandardAnimController::CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript*)
{
    switch (name) {
    case kPlayAnim: {
        PlayParams p = ReadPlayParams(params);
        PlayAnim(mp_timeline, p.anim, p.start, p.end, p.loopType, p.blend, p.speed);
        mp_frameData = LoggedSourceFrameOf(mp_source, m_animSet, p.anim);
        m_frameIndex = FrameIndex(mp_frameData);
        return 1;
    }
    case kNewSource: {
        uint32_t anim = reinterpret_cast<uint32_t>(params); // (the argument's slot, as above)
        params->GetChecksum(kAnim, &anim, 1);
        if (mp_frameData)
            ReleaseFrameIndex(mp_frameData, m_frameIndex);
        if (mp_source) {
            FreeSource(mp_source);
            mp_source = nullptr;
        }
        mp_source = NewSource();
        SourceFrame(mp_source, false);
        mp_frameData = LoggedSourceFrameOf(mp_source, m_animSet, anim);
        m_frameIndex = FrameIndex(mp_frameData);
        return 1;
    }
    case kGlobalFrame: {
        void* global = GameFunction<void*(__cdecl*)()>(0x00480660)();
        void* value = GameFunction<void*(__thiscall*)(void*)>(0x00480800)(global);
        GameFunction<void(__thiscall*)(void*, void*)>(0x00485270)(mp_frameData, value);
        return 1;
    }
    case kInvalidateCache:
        mp_frameData = SourceFrame(mp_source, false);
        return kNotMine;
    default:
        return kNotMine;
    }
}

// 0x004A4620
void CStandardAnimController::Update()
{
    float before = mp_timeline->m_current;
    mp_timeline->m_current = Step(mp_timeline);
    PassedEvents(mp_timeline, before, mp_timeline->m_current);
    if (mp_timeline->m_current < before)
        mp_frameData = SourceFrame(mp_source, false);
}

// --- the weighted controller ---------------------------------------------------------------------------------

// 0x004A5010
CWeightedAnimController* CWeightedAnimController::Construct(CAnimTimeline* timeline, Obj::CCompositeObject* object)
{
    BaseConstruct(this, timeline);
    m_vtable = Vtable(0x00649F68);
    mp_object = timeline ? ObjectOf(this) : object;
    mp_source = NewSource();
    mp_frameData = SourceFrame(mp_source, false);
    m_animSet = AnimSet(mp_object);
    mp_frameData = nullptr;
    m_weight = 0.0f;
    return this;
}

// 0x004A4820
void CWeightedAnimController::DestroyBody()
{
    m_vtable = Vtable(0x00649F68);
    if (mp_source) {
        FreeSource(mp_source);
        mp_source = nullptr;
    }
    mp_frameData = nullptr;
    BaseDestroyBody(this);
}

// 0x004A50B0
CWeightedAnimController* CWeightedAnimController::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x004A5660
bool CWeightedAnimController::GetPose(uint8_t* pose)
{
    if (!mp_frameData || !mp_source || m_weight <= 0.0f)
        return false;
    mp_frameData = SourceFrame(mp_source, false);
    // (the skeleton component's skeleton, +0x18 - read through null without one, as the original)
    auto skeleton = [this] {
        Obj::CBaseComponent* component = mp_object->GetComponent(kSkeleton);
        return component ? *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(component) + 0x18) : nullptr;
    };
    SetBones(mp_source, skeleton());
    SetBoneCount(mp_source, skeleton());
    GameFunction<bool(__thiscall*)(void*, uint8_t*, uint8_t*, uint8_t*, float, void*)>(0x00485250)(
        mp_frameData, pose, pose + 0x400, pose + 0x800, m_weight, mp_source);
    SourceFrame(mp_source, true);
    return true;
}

// 0x004A4890
int CWeightedAnimController::CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript*)
{
    switch (name) {
    case kPlayAnim: {
        uint32_t anim = reinterpret_cast<uint32_t>(params); // (the argument's slot: params' address if missing)
        if (!params->GetChecksum(kWeightedAnim, &anim, 0))
            params->GetChecksum(kAnimName, &anim, 1);
        params->GetFloat(kWeight, &m_weight, 0);
        mp_frameData = SourceFrameOf(mp_source, m_animSet + anim);
        return 1;
    }
    case kSetWeightedAnim: {
        uint32_t anim = 0;
        if (!params->GetChecksum(kWeightedAnim, &anim, 0))
            params->GetChecksum(kAnimName, &anim, 0);
        if (anim)
            mp_frameData = SourceFrameOf(mp_source, m_animSet + anim);
        params->GetFloat(kWeight, &m_weight, 1);
        return 1;
    }
    case kNewSource: {
        uint32_t anim = reinterpret_cast<uint32_t>(params);
        params->GetChecksum(kAnim, &anim, 1);
        if (mp_source) {
            FreeSource(mp_source);
            mp_source = nullptr;
        }
        mp_source = NewSource();
        SourceFrame(mp_source, false);
        mp_frameData = SourceFrameOf(mp_source, m_animSet + anim);
        return 1;
    }
    case kInvalidateCache:
        mp_frameData = SourceFrame(mp_source, false);
        return kNotMine;
    default:
        return kNotMine;
    }
}

// --- the wobble controller -----------------------------------------------------------------------------------

// 0x004A49F0
CWobbleController* CWobbleController::Construct(CAnimTimeline* timeline)
{
    BaseConstruct(this, timeline);
    m_vtable = Vtable(0x00649F80);
    m_target = 0.0f;
    m_ampA = m_ampB = m_k1 = m_k2 = m_spazFactor = 0.0f;
    mp_source = NewSource();
    mp_frameData = SourceFrame(mp_source, false);
    m_animSet = AnimSet(ObjectOf(this));
    return this;
}

// 0x004A4A80
void CWobbleController::DestroyBody()
{
    m_vtable = Vtable(0x00649F80);
    if (mp_source) {
        FreeSource(mp_source);
        mp_source = nullptr;
    }
    if (mp_frameData)
        mp_frameData = nullptr;
    BaseDestroyBody(this);
}

// 0x004A50E0
CWobbleController* CWobbleController::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x004A5110
bool CWobbleController::GetPose(uint8_t* pose)
{
    SetBones(mp_source, SkeletonOf(this));
    SetBoneCount(mp_source, SkeletonOf(this));
    FramePose(mp_frameData, pose, mp_timeline->m_current, -1, mp_source);
    SourceFrame(mp_source, true);
    return true;
}

// 0x004A4AF0
int CWobbleController::CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript*)
{
    switch (name) {
    case kPlayAnim: {
        PlayParams p = ReadPlayParams(params);
        PlayAnim(mp_timeline, p.anim, p.start, p.end, p.loopType, p.blend, p.speed);
        mp_frameData = SourceFrameOf(mp_source, m_animSet + p.anim);
        return 1;
    }
    case kInvalidateCache:
        mp_frameData = SourceFrame(mp_source, false);
        return kNotMine;
    case kSetTarget: {
        float fraction;
        if (params->GetFloat(kFraction, &fraction, 0)) {
            if (fraction < 0.0f)
                fraction = 0.0f;
            else if (fraction > 1.0f)
                fraction = 1.0f;
            const CAnimTimeline* t = mp_timeline;
            m_target = static_cast<float>((static_cast<double>(t->m_end) - t->m_start) * fraction + t->m_start);
        }
        return 1;
    }
    case kSetWobbleDetails: {
        float ampA, ampB, k1, k2, spaz;
        if (params->GetFloat(kWobbleAmpA, &ampA, 0) && params->GetFloat(kWobbleAmpB, &ampB, 0) &&
            params->GetFloat(kWobbleK1, &k1, 0) && params->GetFloat(kWobbleK2, &k2, 0) &&
            params->GetFloat(kSpazFactor, &spaz, 0)) {
            m_ampA = ampA;
            m_ampB = ampB;
            m_k1 = k1;
            m_k2 = k2;
            m_spazFactor = spaz;
        }
        return 1;
    }
    default:
        return kNotMine;
    }
}

// 0x004A5180: the amplitudes grow (spazFactor at the timeline's ends, 1 in its middle) with the target's distance
// from the middle, the target is kept far enough from the ends for them, then the first sine (K1) swings the
// second's amplitude around ampA, and the second (K2) the time around the target; within the timeline.
float CWobbleController::WobbleTime()
{
    const CAnimTimeline* t = mp_timeline;
    float target = m_target;
    float k1 = m_k1, k2 = m_k2;
    double scale = std::fabs(target - (static_cast<double>(t->m_start) + t->m_end) * 0.5) *
                       ((static_cast<double>(m_spazFactor) - 1.0) / ((static_cast<double>(t->m_end) - t->m_start) * 0.5)) +
                   1.0;
    float a = static_cast<float>(scale * m_ampA);
    float b = static_cast<float>(scale * m_ampB);
    if (static_cast<double>(target) - a - b < t->m_start)
        target = static_cast<float>(static_cast<double>(b) + a + t->m_start);
    if (static_cast<double>(target) + b + a > t->m_end)
        target = static_cast<float>(static_cast<double>(t->m_end) - a - b);

    double cycles = static_cast<double>(Clock()) * k1;
    double phase = (cycles - static_cast<double>(Truncated(cycles)) * 0.0625 * 16.0) * TwoPi();
    float amplitude = static_cast<float>(X87Sin(phase) * b + a);
    cycles = static_cast<double>(Clock()) * k2;
    phase = (cycles - static_cast<double>(Truncated(cycles)) * 0.0625 * 16.0) * TwoPi();
    double time = X87Sin(phase) * amplitude + target;
    if (time < t->m_start)
        time = t->m_start;
    if (time > t->m_end)
        time = t->m_end;
    return static_cast<float>(time);
}

// 0x004A52E0
void CWobbleController::Update()
{
    float before = mp_timeline->m_current;
    mp_timeline->m_current = WobbleTime();
    if (mp_timeline->m_current < before)
        mp_frameData = SourceFrame(mp_source, false);
}

// --- the pose controller -------------------------------------------------------------------------------------

// 0x004A5730
CPoseController* CPoseController::Construct(CAnimTimeline* timeline)
{
    BaseConstruct(this, timeline);
    m_vtable = Vtable(0x00649F98);
    auto* rotations = reinterpret_cast<float*>(m_pose);
    for (int i = 0; i < 64; ++i) {
        rotations[i * 4 + 0] = 0.0f;
        rotations[i * 4 + 1] = 0.0f;
        rotations[i * 4 + 2] = 0.0f;
        rotations[i * 4 + 3] = 1.0f;
    }
    return this;
}

// 0x004A4D20
void CPoseController::DestroyBody()
{
    m_vtable = Vtable(0x00649F98);
    BaseDestroyBody(this);
}

// 0x004A5320
CPoseController* CPoseController::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x004A4D30
bool CPoseController::GetPose(uint8_t* pose)
{
    std::memcpy(pose, m_pose, sizeof(m_pose));
    return true;
}

// --- the partial animation controller ------------------------------------------------------------------------

// 0x004A5350
CPartialAnimController* CPartialAnimController::Construct(CAnimTimeline* timeline)
{
    BaseConstruct(this, timeline);
    m_vtable = Vtable(0x00649FB0);
    mp_source = NewSource();
    mp_frameData = SourceFrame(mp_source, false);
    m_animSet = AnimSet(ObjectOf(this));
    void* memory = Mem::Calloc(sizeof(CAnimTimeline));
    mp_partial = memory ? GameFunction<CAnimTimeline*(__thiscall*)(void*)>(0x00481650)(memory) : nullptr;
    mp_frameData = nullptr;
    return this;
}

// 0x004A4D50
void CPartialAnimController::DestroyBody()
{
    m_vtable = Vtable(0x00649FB0);
    if (mp_source) {
        FreeSource(mp_source);
        mp_source = nullptr;
    }
    if (mp_partial) {
        reinterpret_cast<void*(__thiscall*)(CAnimTimeline*, int)>(mp_partial->m_vtable[0])(mp_partial, 1);
        mp_partial = nullptr;
    }
    if (mp_frameData)
        mp_frameData = nullptr;
    BaseDestroyBody(this);
}

// 0x004A5400
CPartialAnimController* CPartialAnimController::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x004A5430
bool CPartialAnimController::GetPose(uint8_t* pose)
{
    SetBones(mp_source, SkeletonOf(this));
    SetBoneCount(mp_source, SkeletonOf(this));
    FramePose(mp_frameData, pose, mp_partial->m_current, -1, mp_source);
    SourceFrame(mp_source, true);
    return true;
}

// 0x004A4DD0
void CPartialAnimController::InitFromStructure(Script::CStruct* params)
{
    BaseInit(this, params);
    uint32_t anim = 0;
    params->GetChecksum(kAnimName, &anim, 1);
    float start = 0.0f, end = 0.0f, current = 0.0f;
    Obj::CBaseComponent* blender = ObjectOf(this)->GetComponent(kAnimBlender);
    // (the blender's GetValue: the anim set's entry for the animation - its length)
    float duration = static_cast<Obj::CAnimBlenderComponent*>(blender)->GetValue(anim);
    // the times and loop type given (0x004877E0, 0x00487770; cdecl)
    GameFunction<void(__cdecl*)(float*, float*, float, float, CStruct*, int)>(0x004877E0)(&start, &end, current,
                                                                                          duration, params, 0);
    int loopType = 0; // (the original leaves it to 0x00487770)
    GameFunction<void(__cdecl*)(int*, CStruct*)>(0x00487770)(&loopType, params);
    float speed = 1.0f;
    params->GetFloat(kSpeed, &speed, 0);
    params->GetFloat(kCurrent, &current, 0);
    // the timeline's PlayAnim (vtable +0x0C)
    reinterpret_cast<void(__thiscall*)(CAnimTimeline*, uint32_t, float, float, int, float, float)>(
        mp_partial->m_vtable[3])(mp_partial, anim, start, end, loopType, 0.0f, speed);
    if (current != 0.0f)
        GameFunction<void(__thiscall*)(CAnimTimeline*, float)>(0x004816B0)(mp_partial, current);
    mp_frameData = SourceFrameOf(mp_source, m_animSet + anim);
}

// 0x004A54A0
int CPartialAnimController::CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script)
{
    switch (name) {
    case kStep: {
        // the timeline stepped once by `frames` (its speed held at 0 meanwhile)
        float frames = 0.0f; // (missing: the original's is whatever its argument slot held)
        params->GetFloat(kFrames, &frames, 1);
        float speed = static_cast<float>(static_cast<double>(mp_partial->m_speed) * FramesPerSecond());
        mp_partial->m_speed = 0.0f;
        mp_partial->m_current = static_cast<float>(static_cast<double>(frames) + mp_partial->m_current);
        reinterpret_cast<void(__thiscall*)(CAnimTimeline*)>(mp_partial->m_vtable[2])(mp_partial);
        mp_partial->m_speed = static_cast<float>(static_cast<double>(speed) * SecondsPerFrame());
        return 1;
    }
    case kSetSpeed: {
        float speed = 0.0f;
        params->GetFloat(kSpeed, &speed, 1);
        mp_partial->m_speed = static_cast<float>(static_cast<double>(speed) * SecondsPerFrame());
        return 1;
    }
    case kInvalidateCache:
        mp_frameData = SourceFrame(mp_source, false);
        return kNotMine;
    case kGetPartialAnimParams: {
        script->m_params->AddFloat(kPartialAnimTime, mp_partial->m_current);
        script->m_params->AddChecksum(kPartialAnim, mp_partial->m_anim);
        CStruct* out = script->m_params;
        out->AddInteger(kPartialAnimComplete, IsComplete(mp_partial));
        return 1;
    }
    case kReverse:
        mp_partial->m_direction = mp_partial->m_direction ? 0 : -1;
        return 1;
    default:
        return kNotMine;
    }
}

// 0x004A4EF0
void CPartialAnimController::Update()
{
    float before = mp_partial->m_current;
    mp_partial->m_current = Step(mp_partial);
    if (mp_partial->m_current < before)
        mp_frameData = SourceFrame(mp_source, false);
}

} // namespace Gfx

using namespace Gfx;

REPLACE(0x004A4610, &CAnimController::BaseInitFromStructure);
REPLACE(0x004A4F30, static_cast<void* (*)(size_t, int)>(&CStandardAnimController::operator new));
REPLACE(0x004A5600, static_cast<void (*)(void*)>(&CStandardAnimController::operator delete));
REPLACE(0x004A4500, &CStandardAnimController::Construct);
REPLACE(0x004A4590, &CStandardAnimController::DestroyBody);
REPLACE(0x004A55E0, &CStandardAnimController::Destroy);
REPLACE(0x004A4F70, &CStandardAnimController::GetPose);
REPLACE(0x004A4670, &CStandardAnimController::CallMemberFunction);
REPLACE(0x004A4620, &CStandardAnimController::Update);
REPLACE(0x004A5010, &CWeightedAnimController::Construct);
REPLACE(0x004A4820, &CWeightedAnimController::DestroyBody);
REPLACE(0x004A50B0, &CWeightedAnimController::Destroy);
REPLACE(0x004A5660, &CWeightedAnimController::GetPose);
REPLACE(0x004A4890, &CWeightedAnimController::CallMemberFunction);
REPLACE(0x004A49F0, &CWobbleController::Construct);
REPLACE(0x004A4A80, &CWobbleController::DestroyBody);
REPLACE(0x004A50E0, &CWobbleController::Destroy);
REPLACE(0x004A5110, &CWobbleController::GetPose);
REPLACE(0x004A4AF0, &CWobbleController::CallMemberFunction);
REPLACE(0x004A5180, &CWobbleController::WobbleTime);
REPLACE(0x004A52E0, &CWobbleController::Update);
REPLACE(0x004A5730, &CPoseController::Construct);
REPLACE(0x004A4D20, &CPoseController::DestroyBody);
REPLACE(0x004A5320, &CPoseController::Destroy);
REPLACE(0x004A4D30, &CPoseController::GetPose);
REPLACE(0x004A5350, &CPartialAnimController::Construct);
REPLACE(0x004A4D50, &CPartialAnimController::DestroyBody);
REPLACE(0x004A5400, &CPartialAnimController::Destroy);
REPLACE(0x004A5430, &CPartialAnimController::GetPose);
REPLACE(0x004A4DD0, &CPartialAnimController::InitFromStructure);
REPLACE(0x004A54A0, &CPartialAnimController::CallMemberFunction);
REPLACE(0x004A4EF0, &CPartialAnimController::Update);
