// The animation controllers (standard, weighted, wobble, pose, partial animation): see anim_controller.h.

#include "game/gfx/anim_controller.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "game/core/mem.h"
#include "game/core/slab_bank.h"
#include "game/gfx/anim_cache.h"
#include "game/gfx/anim_track.h"
#include "game/gfx/boned_anim_frame_data.h"
#include "game/gfx/engine_glue.h"
#include "game/gfx/frame_source.h"
#include "game/gfx/model_utils.h"
#include "game/gfx/scene_load.h"
#include "game/obj/anim_blender.h"
#include "game/obj/entity.h"
#include "game/script/cscript.h"
#include "game/script/param_struct.h"
#include "game/script/names.h"
#include "game/sys/game_clock.h"
#include "game/core/code_address.h"
#include "game/sys/run_log.h"
#include "game/skater/vehicle_sound.h"
#include "game/core/stubs.h"
#include "game/front/screen_constants.h"
#include "game/obj/control_pad.h"

namespace Gfx2D {

namespace {
extern void** const kCStandardAnimControllerVtable;
extern void** const kCWeightedAnimControllerVtable;
extern void** const kCWobbleControllerVtable;
extern void** const kCPoseControllerVtable;
extern void** const kCPartialAnimControllerVtable;
} // namespace

MemSys::PoolPair g_standardPools; // (CBonedAnimController; script_startup.h)

namespace {

using Scripts::CStruct;

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

float FramesPerSecond() { return MenuSystem::g_framesPerSecond; } // 60
float SecondsPerFrame() { return ::GameObjects::g_animMoviesSecondsPerFrame; } // 1/60
float TwoPi() { return MenuSystem::g_twoPi; }

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

// The base controller.
void BaseConstruct(CAnimController* c, CAnimTimeline* timeline) { c->CAnimController::MakeNew(timeline); }
void BaseDestroyBody(CAnimController* c) { c->CAnimController::DestroyBody(); }
void BaseInit(CAnimController* c, CStruct* params) { c->CAnimController::ConfigureFromData(params); }
// The object (the blend timeline's +0x64) and its skeleton (the Skeleton component's +0x18, or null).
GameObjects::CGameEntity* ObjectOf(CAnimController* c) { return c->CAnimController::FetchSubject(); }
uint8_t* SkeletonOf(CAnimController* c) { return c->CAnimController::FetchRig(); }
// The object's anim set (the animation blender component's 0x00419AB0) and an animation's length in it.
uint32_t AnimSet(GameObjects::CGameEntity* object)
{
    GameObjects::CObjectModule* blender = object->FetchModule(kAnimBlender);
    return static_cast<GameObjects::CAnimBlenderComponent*>(blender)->GetAnimSet();
}

// The timeline's pieces (thiscall, not rewritten yet).
float Step(CAnimTimeline* t) { return static_cast<float>(t->NextTime()); }
void PassedEvents(CAnimTimeline* t, float from, float to)
{
    static_cast<CAnimTrack*>(t)->FireEvents(from, to);
}
void PlayAnim(CAnimTimeline* t, uint32_t anim, float start, float end, int loopType, float blend, float speed)
{
    t->PlayAnim(anim, start, end, loopType, blend, speed);
}
bool IsAccomplished(CAnimTimeline* t) { return t->IsAccomplished(); }

// Frame sources (0x0048BFC0 makes one, 0x0048BFD0 frees it) and their frames.
void* NewSource() { return Engine3D::CreateFrameSource(); }
void FreeSource(void* source) { Engine3D::DeleteObject(source); }
void* SourceFrame(void* source, bool done) { return static_cast<Engine3D::CFrameSource*>(source)->SetDone(done); }
void* SourceFrameOf(void* source, uint32_t anim) { return static_cast<Engine3D::CFrameSource*>(source)->PlayClip(anim); }
int FrameIndex(void* frame) { return static_cast<CJointClipFrames*>(frame)->AcquireCacheIndex(); }
// NOT ORIGINAL - the frame of an animation the source's set hasn't is null, and FrameIndex reads through it (a crash at
// 0x00482BA2, as in the original: e.g. peds whose animations weren't loaded, or a converted level's script playing one
// the skeleton hasn't): logged first, with the animation's name.
void* LoggedSourceFrameOf(void* source, uint32_t set, uint32_t anim)
{
    void* frame = SourceFrameOf(source, set + anim);
    if (!frame) {
        const char* name = Scripts::LookUpHashName(anim);
        Log("anim: no animation 0x%08X%s%s%s in the set 0x%08X - the next step (FrameIndex 0x00482BA0) crashes", anim,
            name ? " (" : "", name ? name : "", name ? ")" : "", set);
    }
    return frame;
}
void ReleaseFrameIndex(void* frame, int index) { static_cast<CJointClipFrames*>(frame)->ReleaseCacheIndex(index); }
void FramePose(void* frame, uint8_t* pose, float time, int index, void* source)
{
    static_cast<CJointClipFrames*>(frame)->GetFrames(reinterpret_cast<Maths::QuatRotation*>(pose),
                                                        reinterpret_cast<Maths::Vec4*>(pose + 0x400), pose + 0x800, time,
                                                        index, source);
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
int Clock() { return FrameClock::GameClockElapsed() + GameObjects::g_clockOffset; }

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
    params->ReadHashValue(kAnimName, &p.anim, 1);
    params->FetchFloat(kStartTime, &p.start, 1);
    params->FetchFloat(kEndTime, &p.end, 1);
    params->FetchIntValue(kLoopType, &p.loopType, 1);
    params->FetchFloat(kBlendPeriod, &p.blend, 1);
    params->FetchFloat(kSpeed, &p.speed, 1);
    return p;
}

MemSys::PoolPair& StandardPools() { return g_standardPools; }

} // namespace

// 0x004A4610
void CAnimController::BaseInitFromStructure(Scripts::CStruct* params) { BaseInit(this, params); }

// --- the standard controller ---------------------------------------------------------------------------------

// 0x004A4F30
void* CStandardAnimController::operator new(size_t size, int)
{
    return StandardPools().Carve(static_cast<unsigned int>(size));
}

// 0x004A5600
void CStandardAnimController::operator delete(void* item) { StandardPools().Dealloc(item); }

// 0x004A4500
CStandardAnimController* CStandardAnimController::MakeNew(CAnimTimeline* timeline)
{
    BaseConstruct(this, timeline);
    m_vtable = kCStandardAnimControllerVtable;
    mp_source = NewSource();
    m_frameSource = SourceFrame(mp_source, false);
    m_animSet = AnimSet(ObjectOf(this));
    m_frameSource = nullptr;
    m_frameIndex = -1;
    return this;
}

// 0x004A4590
void CStandardAnimController::DestroyBody()
{
    m_vtable = kCStandardAnimControllerVtable;
    if (m_frameSource)
        ReleaseFrameIndex(m_frameSource, m_frameIndex);
    if (mp_source) {
        FreeSource(mp_source);
        mp_source = nullptr;
    }
    m_frameSource = nullptr;
    BaseDestroyBody(this);
}

// 0x004A55E0
CStandardAnimController* CStandardAnimController::Teardown(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        operator delete(this);
    return this;
}

// 0x004A4F70
bool CStandardAnimController::FetchPose(uint8_t* pose)
{
    if (m_frameIndex == -1) {
        if (*reinterpret_cast<int*>(static_cast<uint8_t*>(m_frameSource) + 0x24))
            m_frameIndex = FrameIndex(m_frameSource);
        else
            m_frameSource = SourceFrame(mp_source, false);
    }
    SetBones(mp_source, SkeletonOf(this));
    SetBoneCount(mp_source, SkeletonOf(this));
    FramePose(m_frameSource, pose, mp_timeline->m_ongoing, m_frameIndex, mp_source);
    SourceFrame(mp_source, true);
    return true;
}

// 0x004A4670
int CStandardAnimController::InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript*)
{
    switch (name) {
    case kPlayAnim: {
        PlayParams p = ReadPlayParams(params);
        PlayAnim(mp_timeline, p.anim, p.start, p.end, p.loopType, p.blend, p.speed);
        m_frameSource = LoggedSourceFrameOf(mp_source, m_animSet, p.anim);
        m_frameIndex = FrameIndex(m_frameSource);
        return 1;
    }
    case kNewSource: {
        uint32_t anim = reinterpret_cast<uint32_t>(params); // (the argument's slot, as above)
        params->ReadHashValue(kAnim, &anim, 1);
        if (m_frameSource)
            ReleaseFrameIndex(m_frameSource, m_frameIndex);
        if (mp_source) {
            FreeSource(mp_source);
            mp_source = nullptr;
        }
        mp_source = NewSource();
        SourceFrame(mp_source, false);
        m_frameSource = LoggedSourceFrameOf(mp_source, m_animSet, anim);
        m_frameIndex = FrameIndex(m_frameSource);
        return 1;
    }
    case kGlobalFrame: {
        int value = CClipStore::Pull()->FetchKind();
        static_cast<CJointClipFrames*>(m_frameSource)->BuildCache(value);
        return 1;
    }
    case kInvalidateCache:
        m_frameSource = SourceFrame(mp_source, false);
        return kNotMine;
    default:
        return kNotMine;
    }
}

// 0x004A4620
void CStandardAnimController::TickFrame()
{
    float before = mp_timeline->m_ongoing;
    mp_timeline->m_ongoing = Step(mp_timeline);
    PassedEvents(mp_timeline, before, mp_timeline->m_ongoing);
    if (mp_timeline->m_ongoing < before)
        m_frameSource = SourceFrame(mp_source, false);
}

// --- the weighted controller ---------------------------------------------------------------------------------

// 0x004A5010
CWeightedAnimController* CWeightedAnimController::MakeNew(CAnimTimeline* timeline, GameObjects::CGameEntity* object)
{
    BaseConstruct(this, timeline);
    m_vtable = kCWeightedAnimControllerVtable;
    m_ownerObject = timeline ? ObjectOf(this) : object;
    mp_source = NewSource();
    m_frameSource = SourceFrame(mp_source, false);
    m_animSet = AnimSet(m_ownerObject);
    m_frameSource = nullptr;
    m_weight = 0.0f;
    return this;
}

// 0x004A4820
void CWeightedAnimController::DestroyBody()
{
    m_vtable = kCWeightedAnimControllerVtable;
    if (mp_source) {
        FreeSource(mp_source);
        mp_source = nullptr;
    }
    m_frameSource = nullptr;
    BaseDestroyBody(this);
}

// 0x004A50B0
CWeightedAnimController* CWeightedAnimController::Teardown(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x004A5660
bool CWeightedAnimController::FetchPose(uint8_t* pose)
{
    if (!m_frameSource || !mp_source || m_weight <= 0.0f)
        return false;
    m_frameSource = SourceFrame(mp_source, false);
    // (the skeleton component's skeleton, +0x18 - read through null without one, as the original)
    auto skeleton = [this] {
        GameObjects::CObjectModule* component = m_ownerObject->FetchModule(kSkeleton);
        return component ? *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(component) + 0x18) : nullptr;
    };
    SetBones(mp_source, skeleton());
    SetBoneCount(mp_source, skeleton());
    static_cast<CJointClipFrames*>(m_frameSource)
        ->ApplyAdditiveIfCompressed(reinterpret_cast<Maths::QuatRotation*>(pose), reinterpret_cast<Maths::Vec4*>(pose + 0x400),
                                    pose + 0x800, m_weight, static_cast<uint8_t*>(mp_source));
    SourceFrame(mp_source, true);
    return true;
}

// 0x004A4890
int CWeightedAnimController::InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript*)
{
    switch (name) {
    case kPlayAnim: {
        uint32_t anim = reinterpret_cast<uint32_t>(params); // (the argument's slot: params' address if missing)
        if (!params->ReadHashValue(kWeightedAnim, &anim, 0))
            params->ReadHashValue(kAnimName, &anim, 1);
        params->FetchFloat(kWeight, &m_weight, 0);
        m_frameSource = SourceFrameOf(mp_source, m_animSet + anim);
        return 1;
    }
    case kSetWeightedAnim: {
        uint32_t anim = 0;
        if (!params->ReadHashValue(kWeightedAnim, &anim, 0))
            params->ReadHashValue(kAnimName, &anim, 0);
        if (anim)
            m_frameSource = SourceFrameOf(mp_source, m_animSet + anim);
        params->FetchFloat(kWeight, &m_weight, 1);
        return 1;
    }
    case kNewSource: {
        uint32_t anim = reinterpret_cast<uint32_t>(params);
        params->ReadHashValue(kAnim, &anim, 1);
        if (mp_source) {
            FreeSource(mp_source);
            mp_source = nullptr;
        }
        mp_source = NewSource();
        SourceFrame(mp_source, false);
        m_frameSource = SourceFrameOf(mp_source, m_animSet + anim);
        return 1;
    }
    case kInvalidateCache:
        m_frameSource = SourceFrame(mp_source, false);
        return kNotMine;
    default:
        return kNotMine;
    }
}

// --- the wobble controller -----------------------------------------------------------------------------------

// 0x004A49F0
CWobbleDriver* CWobbleDriver::MakeNew(CAnimTimeline* timeline)
{
    BaseConstruct(this, timeline);
    m_vtable = kCWobbleControllerVtable;
    m_aimPoint = 0.0f;
    m_ampA = m_ampB = m_k1 = m_k2 = m_spazFactor = 0.0f;
    mp_source = NewSource();
    m_frameSource = SourceFrame(mp_source, false);
    m_animSet = AnimSet(ObjectOf(this));
    return this;
}

// 0x004A4A80
void CWobbleDriver::DestroyBody()
{
    m_vtable = kCWobbleControllerVtable;
    if (mp_source) {
        FreeSource(mp_source);
        mp_source = nullptr;
    }
    if (m_frameSource)
        m_frameSource = nullptr;
    BaseDestroyBody(this);
}

// 0x004A50E0
CWobbleDriver* CWobbleDriver::Teardown(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x004A5110
bool CWobbleDriver::FetchPose(uint8_t* pose)
{
    SetBones(mp_source, SkeletonOf(this));
    SetBoneCount(mp_source, SkeletonOf(this));
    FramePose(m_frameSource, pose, mp_timeline->m_ongoing, -1, mp_source);
    SourceFrame(mp_source, true);
    return true;
}

// 0x004A4AF0
int CWobbleDriver::InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript*)
{
    switch (name) {
    case kPlayAnim: {
        PlayParams p = ReadPlayParams(params);
        PlayAnim(mp_timeline, p.anim, p.start, p.end, p.loopType, p.blend, p.speed);
        m_frameSource = SourceFrameOf(mp_source, m_animSet + p.anim);
        return 1;
    }
    case kInvalidateCache:
        m_frameSource = SourceFrame(mp_source, false);
        return kNotMine;
    case kSetTarget: {
        float fraction;
        if (params->FetchFloat(kFraction, &fraction, 0)) {
            if (fraction < 0.0f)
                fraction = 0.0f;
            else if (fraction > 1.0f)
                fraction = 1.0f;
            const CAnimTimeline* t = mp_timeline;
            m_aimPoint = static_cast<float>((static_cast<double>(t->m_finish) - t->m_outset) * fraction + t->m_outset);
        }
        return 1;
    }
    case kSetWobbleDetails: {
        float ampA, ampB, k1, k2, spaz;
        if (params->FetchFloat(kWobbleAmpA, &ampA, 0) && params->FetchFloat(kWobbleAmpB, &ampB, 0) &&
            params->FetchFloat(kWobbleK1, &k1, 0) && params->FetchFloat(kWobbleK2, &k2, 0) &&
            params->FetchFloat(kSpazFactor, &spaz, 0)) {
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
float CWobbleDriver::WobbleTime()
{
    const CAnimTimeline* t = mp_timeline;
    float target = m_aimPoint;
    float k1 = m_k1, k2 = m_k2;
    double scale = std::fabs(target - (static_cast<double>(t->m_outset) + t->m_finish) * 0.5) *
                       ((static_cast<double>(m_spazFactor) - 1.0) / ((static_cast<double>(t->m_finish) - t->m_outset) * 0.5)) +
                   1.0;
    float a = static_cast<float>(scale * m_ampA);
    float b = static_cast<float>(scale * m_ampB);
    if (static_cast<double>(target) - a - b < t->m_outset)
        target = static_cast<float>(static_cast<double>(b) + a + t->m_outset);
    if (static_cast<double>(target) + b + a > t->m_finish)
        target = static_cast<float>(static_cast<double>(t->m_finish) - a - b);

    double cycles = static_cast<double>(Clock()) * k1;
    double phase = (cycles - static_cast<double>(Truncated(cycles)) * 0.0625 * 16.0) * TwoPi();
    float amplitude = static_cast<float>(X87Sin(phase) * b + a);
    cycles = static_cast<double>(Clock()) * k2;
    phase = (cycles - static_cast<double>(Truncated(cycles)) * 0.0625 * 16.0) * TwoPi();
    double time = X87Sin(phase) * amplitude + target;
    if (time < t->m_outset)
        time = t->m_outset;
    if (time > t->m_finish)
        time = t->m_finish;
    return static_cast<float>(time);
}

// 0x004A52E0
void CWobbleDriver::TickFrame()
{
    float before = mp_timeline->m_ongoing;
    mp_timeline->m_ongoing = WobbleTime();
    if (mp_timeline->m_ongoing < before)
        m_frameSource = SourceFrame(mp_source, false);
}

// --- the pose controller -------------------------------------------------------------------------------------

// 0x004A5730
CStanceDriver* CStanceDriver::MakeNew(CAnimTimeline* timeline)
{
    BaseConstruct(this, timeline);
    m_vtable = kCPoseControllerVtable;
    auto* rotations = reinterpret_cast<float*>(m_stance);
    for (int i = 0; i < 64; ++i) {
        rotations[i * 4 + 0] = 0.0f;
        rotations[i * 4 + 1] = 0.0f;
        rotations[i * 4 + 2] = 0.0f;
        rotations[i * 4 + 3] = 1.0f;
    }
    return this;
}

// 0x004A4D20
void CStanceDriver::DestroyBody()
{
    m_vtable = kCPoseControllerVtable;
    BaseDestroyBody(this);
}

// 0x004A5320
CStanceDriver* CStanceDriver::Teardown(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x004A4D30
bool CStanceDriver::FetchPose(uint8_t* pose)
{
    std::memcpy(pose, m_stance, sizeof(m_stance));
    return true;
}

// --- the partial animation controller ------------------------------------------------------------------------

// 0x004A5350
CPartialClipDriver* CPartialClipDriver::MakeNew(CAnimTimeline* timeline)
{
    BaseConstruct(this, timeline);
    m_vtable = kCPartialAnimControllerVtable;
    mp_source = NewSource();
    m_frameSource = SourceFrame(mp_source, false);
    m_animSet = AnimSet(ObjectOf(this));
    void* memory = MemSys::AllocZeroed(sizeof(CAnimTimeline));
    mp_partial = memory ? static_cast<CAnimTimeline*>(memory)->MakeNew() : nullptr;
    m_frameSource = nullptr;
    return this;
}

// 0x004A4D50
void CPartialClipDriver::DestroyBody()
{
    m_vtable = kCPartialAnimControllerVtable;
    if (mp_source) {
        FreeSource(mp_source);
        mp_source = nullptr;
    }
    if (mp_partial) {
        reinterpret_cast<void*(__thiscall*)(CAnimTimeline*, int)>(mp_partial->m_vtable[0])(mp_partial, 1);
        mp_partial = nullptr;
    }
    if (m_frameSource)
        m_frameSource = nullptr;
    BaseDestroyBody(this);
}

// 0x004A5400
CPartialClipDriver* CPartialClipDriver::Teardown(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x004A5430
bool CPartialClipDriver::FetchPose(uint8_t* pose)
{
    SetBones(mp_source, SkeletonOf(this));
    SetBoneCount(mp_source, SkeletonOf(this));
    FramePose(m_frameSource, pose, mp_partial->m_ongoing, -1, mp_source);
    SourceFrame(mp_source, true);
    return true;
}

// 0x004A4DD0
void CPartialClipDriver::ConfigureFromData(Scripts::CStruct* params)
{
    BaseInit(this, params);
    uint32_t anim = 0;
    params->ReadHashValue(kAnimName, &anim, 1);
    float start = 0.0f, end = 0.0f, current = 0.0f;
    GameObjects::CObjectModule* blender = ObjectOf(this)->FetchModule(kAnimBlender);
    // (the blender's FetchValue: the anim set's entry for the animation - its length)
    float duration = static_cast<GameObjects::CAnimBlenderComponent*>(blender)->FetchValue(anim);
    // the times and loop type given (0x004877E0, 0x00487770; cdecl)
    FetchAnimDurations(&start, &end, current, duration, params);
    int loopType = 0; // (the original leaves it to 0x00487770)
    GetAnimLoopType(&loopType, params);
    float speed = 1.0f;
    params->FetchFloat(kSpeed, &speed, 0);
    params->FetchFloat(kCurrent, &current, 0);
    // the timeline's PlayAnim (vtable +0x0C)
    reinterpret_cast<void(__thiscall*)(CAnimTimeline*, uint32_t, float, float, int, float, float)>(
        mp_partial->m_vtable[3])(mp_partial, anim, start, end, loopType, 0.0f, speed);
    if (current != 0.0f)
        mp_partial->AdvanceClock(current);
    m_frameSource = SourceFrameOf(mp_source, m_animSet + anim);
}

// 0x004A54A0
int CPartialClipDriver::InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript* script)
{
    switch (name) {
    case kStep: {
        // the timeline stepped once by `frames` (its speed held at 0 meanwhile)
        float frames = 0.0f; // (missing: the original's is whatever its argument slot held)
        params->FetchFloat(kFrames, &frames, 1);
        float speed = static_cast<float>(static_cast<double>(mp_partial->m_speed) * FramesPerSecond());
        mp_partial->m_speed = 0.0f;
        mp_partial->m_ongoing = static_cast<float>(static_cast<double>(frames) + mp_partial->m_ongoing);
        reinterpret_cast<void(__thiscall*)(CAnimTimeline*)>(mp_partial->m_vtable[2])(mp_partial);
        mp_partial->m_speed = static_cast<float>(static_cast<double>(speed) * SecondsPerFrame());
        return 1;
    }
    case kSetSpeed: {
        float speed = 0.0f;
        params->FetchFloat(kSpeed, &speed, 1);
        mp_partial->m_speed = static_cast<float>(static_cast<double>(speed) * SecondsPerFrame());
        return 1;
    }
    case kInvalidateCache:
        m_frameSource = SourceFrame(mp_source, false);
        return kNotMine;
    case kGetPartialAnimParams: {
        script->m_args->PushFloat(kPartialAnimTime, mp_partial->m_ongoing);
        script->m_args->PutNameKey(kPartialAnim, mp_partial->m_anim);
        CStruct* out = script->m_args;
        out->PutInt(kPartialAnimComplete, IsAccomplished(mp_partial));
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
void CPartialClipDriver::TickFrame()
{
    float before = mp_partial->m_ongoing;
    mp_partial->m_ongoing = Step(mp_partial);
    if (mp_partial->m_ongoing < before)
        m_frameSource = SourceFrame(mp_source, false);
}

namespace {
static VtableSlot kCStandardAnimControllerVtable_slots[] = {&CStandardAnimController::Teardown,
                &CStandardAnimController::FetchPose, Stubs::IgnoreArgument, &CStandardAnimController::InvokeMethodByName,
                &CAnimController::BaseInitFromStructure, &CStandardAnimController::TickFrame};
void** const kCStandardAnimControllerVtable = reinterpret_cast<void**>(kCStandardAnimControllerVtable_slots);
static VtableSlot kCWeightedAnimControllerVtable_slots[] = {&CWeightedAnimController::Teardown,
                &CWeightedAnimController::FetchPose, Stubs::IgnoreArgument, &CWeightedAnimController::InvokeMethodByName,
                &CAnimController::BaseInitFromStructure, Stubs::DoNothing};
void** const kCWeightedAnimControllerVtable = reinterpret_cast<void**>(kCWeightedAnimControllerVtable_slots);
static VtableSlot kCWobbleControllerVtable_slots[] = {&CWobbleDriver::Teardown, &CWobbleDriver::FetchPose,
                Stubs::IgnoreArgument, &CWobbleDriver::InvokeMethodByName, &CAnimController::BaseInitFromStructure,
                &CWobbleDriver::TickFrame};
void** const kCWobbleControllerVtable = reinterpret_cast<void**>(kCWobbleControllerVtable_slots);
static VtableSlot kCPoseControllerVtable_slots[] = {&CStanceDriver::Teardown, &CStanceDriver::FetchPose,
                Stubs::IgnoreArgument, &GameObjects::ComponentWithoutMemberFunctions::InvokeMethodByName,
                &CAnimController::BaseInitFromStructure, Stubs::DoNothing};
void** const kCPoseControllerVtable = reinterpret_cast<void**>(kCPoseControllerVtable_slots);
static VtableSlot kCPartialAnimControllerVtable_slots[] = {&CPartialClipDriver::Teardown,
                &CPartialClipDriver::FetchPose, Stubs::IgnoreArgument, &CPartialClipDriver::InvokeMethodByName,
                &CPartialClipDriver::ConfigureFromData, &CPartialClipDriver::TickFrame};
void** const kCPartialAnimControllerVtable = reinterpret_cast<void**>(kCPartialAnimControllerVtable_slots);
} // namespace

} // namespace Gfx

using namespace Gfx2D;
