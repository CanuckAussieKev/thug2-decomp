// The object and camera animation movies, 0x005871B0-0x005880DF. See anim_movies.h.

#include "game/obj/anim_movies.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

constexpr uint32_t kObjectAnimMovieVtable = 0x0064EFE8;
constexpr uint32_t kCameraAnimMovieVtable = 0x0064F020;
constexpr uint32_t kNoAsset = 0xEE721991;   // a timer, not an asset
constexpr uint32_t kFrames = 0x019176C5;    // its length in frames
constexpr uint32_t kLooping = 0x5EA0E211;
constexpr uint32_t kBackwards = 0xF8CFD515;

using Gfx::CAnimTimeline;

// The base movie's code (0x00468xxx) and the base's CallMemberFunction (0x004A0CF0).
void BaseConstruct(void* movie) { GameFunction<void*(__thiscall*)(void*)>(0x00468FB0)(movie); }
void BaseDestroyBody(void* movie) { GameFunction<void(__thiscall*)(void*)>(0x00469230)(movie); }
bool BaseLoad(void* movie, Script::CStruct* params)
{
    return GameFunction<bool(__thiscall*)(void*, Script::CStruct*)>(0x00469050)(movie, params);
}
void SetHeld(void* movie, int value) { GameFunction<void(__thiscall*)(void*, int)>(0x00468F50)(movie, value); }
uint32_t NameOf(void* movie) { return GameFunction<uint32_t(__thiscall*)(void*)>(0x00543150)(movie); }
// The asset manager's asset by name (null if none).
void* FindAsset(uint32_t name)
{
    return GameFunction<void*(__thiscall*)(void*, uint32_t, int)>(0x00407C00)(GameVariable<void*>(0x006918DC), name, 0);
}
// The movie's own Start (vtable slot 3).
void StartMovie(CMovie* movie) { reinterpret_cast<bool(__thiscall*)(CMovie*)>(movie->Vtable()[3])(movie); }

// The timeline's code: constructor / destructor body, complete, PlayAnim (slot 3), Update (slot 2).
void ConstructTimeline(CAnimTimeline* t) { GameFunction<void*(__thiscall*)(CAnimTimeline*)>(0x00481650)(t); }
void DestroyTimeline(CAnimTimeline* t) { GameFunction<void(__thiscall*)(CAnimTimeline*)>(0x00481690)(t); }
bool IsComplete(CAnimTimeline* t) { return GameFunction<bool(__thiscall*)(CAnimTimeline*)>(0x004813F0)(t); }
void PlayAnim(CAnimTimeline* t, uint32_t anim, float start, float end, int loopType, float blend, float speed)
{
    reinterpret_cast<void(__thiscall*)(CAnimTimeline*, uint32_t, float, float, int, float, float)>(t->m_vtable[3])(
        t, anim, start, end, loopType, blend, speed);
}
void UpdateTimeline(CAnimTimeline* t) { reinterpret_cast<void(__thiscall*)(CAnimTimeline*)>(t->m_vtable[2])(t); }

// The animation asset's code: started (0x00482F10); told a step from one time to another (0x00482F40).
void StartAsset(void* anim) { GameFunction<void(__thiscall*)(void*)>(0x00482F10)(anim); }
void StepAsset(void* anim, float from, float to, Gfx::Camera* camera, bool first)
{
    GameFunction<void(__thiscall*)(void*, float, float, Gfx::Camera*, bool)>(0x00482F40)(anim, from, to, camera, first);
}

float SecondsPerFrame() { return GameVariable<float>(0x006463B8); } // 1/60

// Play: the asset's length (its first word) from 0, or backwards from a frame before its end to 0.
void PlayOn(CAnimTimeline* t, void* anim, int loopType, bool backwards)
{
    float length = *static_cast<float*>(anim);
    if (backwards)
        PlayAnim(t, 0, static_cast<float>(static_cast<double>(length) - SecondsPerFrame()), 0.0f, loopType, 0.0f, 1.0f);
    else
        PlayAnim(t, 0, 0.0f, length, loopType, 0.0f, 1.0f);
}

// Load's shared part: the asset named like the movie played (false: there's none), or a timer of `frames`.
template <typename Movie>
bool LoadAnim(Movie* movie, Script::CStruct* params)
{
    if (!params->ContainsFlag(kNoAsset)) {
        void* anim = FindAsset(NameOf(movie));
        if (!anim)
            return false;
        bool backwards = params->ContainsFlag(kBackwards);
        int loopType = params->ContainsFlag(kLooping) ? 1 : 0;
        movie->Play(anim, loopType, backwards);
    } else {
        int frames = 0;
        params->GetInteger(kFrames, &frames, 1);
        movie->m_anim = nullptr;
        PlayAnim(&movie->m_timeline, 0, 0.0f, static_cast<float>(frames * static_cast<double>(SecondsPerFrame())), 0, 0.0f,
                 1.0f);
    }
    SetHeld(movie, 1);
    return true;
}

} // namespace

// --- the object animation movie -------------------------------------------------------------------------------

// 0x005871B0
CObjectAnimMovie* CObjectAnimMovie::Construct()
{
    BaseConstruct(this);
    m_base.m_node.vtable = reinterpret_cast<void**>(kObjectAnimMovieVtable);
    ConstructTimeline(&m_timeline);
    m_anim = nullptr;
    return this;
}

// 0x00587210
void CObjectAnimMovie::DestroyBody()
{
    m_base.m_node.vtable = reinterpret_cast<void**>(kObjectAnimMovieVtable);
    DestroyTimeline(&m_timeline);
    BaseDestroyBody(this);
}

// 0x00587370
CObjectAnimMovie* CObjectAnimMovie::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x005875D0
bool CObjectAnimMovie::Load(Script::CStruct* params)
{
    BaseLoad(this, params);
    if (!LoadAnim(this, params))
        return false;
    StartMovie(&m_base);
    return true;
}

// 0x005873A0
void CObjectAnimMovie::Play(void* anim, int loopType, bool backwards)
{
    m_anim = anim;
    PlayOn(&m_timeline, anim, loopType, backwards);
}

// 0x00587270
bool CObjectAnimMovie::Start()
{
    if (m_anim)
        StartAsset(m_anim);
    return true;
}

// 0x00587280 / 0x005872A0
bool CObjectAnimMovie::IsDone()
{
    if (m_base.m_unknown1A)
        return true;
    if (m_base.m_unknown18)
        return false;
    return IsComplete(&m_timeline);
}
int CObjectAnimMovie::IsHeldAndComplete() { return m_base.m_unknown18 && IsComplete(&m_timeline) ? 1 : 0; }

// --- the camera animation movie -------------------------------------------------------------------------------

// 0x005873F0
CCameraAnimMovie* CCameraAnimMovie::Construct()
{
    BaseConstruct(this);
    m_base.m_node.vtable = reinterpret_cast<void**>(kCameraAnimMovieVtable);
    ConstructTimeline(&m_timeline);
    m_anim = nullptr;
    m_hasTarget = false;
    m_target = 0;
    reinterpret_cast<uint32_t&>(m_fov) = 0;
    return this;
}

// 0x005872C0
void CCameraAnimMovie::DestroyBody()
{
    m_base.m_node.vtable = reinterpret_cast<void**>(kCameraAnimMovieVtable);
    DestroyTimeline(&m_timeline);
    BaseDestroyBody(this);
}

// 0x00587450
CCameraAnimMovie* CCameraAnimMovie::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00587F70
bool CCameraAnimMovie::Load(Script::CStruct* params)
{
    BaseLoad(this, params);
    float fov;
    if (params->GetFloat(0x17E7321E, &fov, 0)) {
        m_fov = fov;
        GameFunction<void(__thiscall*)(Gfx::Camera*, float)>(0x00485B20)(m_base.m_camera, fov);
    }
    if (!LoadAnim(this, params))
        return false;
    LoadTarget(params);
    StartMovie(&m_base);
    PlaceCamera();
    if (m_anim)
        StepAsset(m_anim, m_timeline.m_current, m_timeline.m_current, m_base.m_camera, true);
    return true;
}

// 0x00587480
void CCameraAnimMovie::LoadTarget(Script::CStruct* params)
{
    m_hasTarget = params->GetChecksum(0x531E4D28, &m_target, 0);
    if (!m_hasTarget)
        return;
    m_targetOffset[0] = m_targetOffset[1] = m_targetOffset[2] = 0.0f;
    m_targetOffset[3] = 1.0f;
    params->GetVector(0x906642BF, reinterpret_cast<Script::VectorValue*>(m_targetOffset), 0);
    float zero = GameVariable<float>(0x0066D520);
    // (not equal, or unordered: a NaN counts as set)
    m_targetOffsetSet = !(m_targetOffset[0] == zero) || !(m_targetOffset[1] == zero) || !(m_targetOffset[2] == zero);
    m_lookOffset[0] = m_lookOffset[1] = m_lookOffset[2] = 0.0f;
    m_lookOffset[3] = 1.0f;
    m_lookOffsetSet = params->GetVector(0xA00D3D6E, reinterpret_cast<Script::VectorValue*>(m_lookOffset), 0);
}

// 0x00587540
void CCameraAnimMovie::Play(void* anim, int loopType, bool backwards)
{
    m_anim = anim;
    PlayOn(&m_timeline, anim, loopType, backwards);
}

// 0x00587320
bool CCameraAnimMovie::Start()
{
    if (m_anim)
        StartAsset(m_anim);
    return true;
}

// 0x00587330 / 0x00587350
bool CCameraAnimMovie::IsDone()
{
    if (m_base.m_unknown1A)
        return true;
    if (m_base.m_unknown18)
        return false;
    return IsComplete(&m_timeline);
}
int CCameraAnimMovie::IsHeldAndComplete() { return m_base.m_unknown18 && IsComplete(&m_timeline) ? 1 : 0; }

// 0x00587590
bool CCameraAnimMovie::CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script)
{
    if (name == 0xA8D5A188) {
        LoadTarget(params);
        return true;
    }
    if (name == 0xD7241A6C) {
        m_hasTarget = false;
        m_target = 0;
        return true;
    }
    return GameFunction<bool(__thiscall*)(void*, uint32_t, Script::CStruct*, Script::CScript*)>(0x004A0CF0)(this, name, params,
                                                                                                         script);
}

// 0x00588090
void CCameraAnimMovie::Update()
{
    float from = m_timeline.m_current;
    UpdateTimeline(&m_timeline);
    GameFunction<void(__thiscall*)(Gfx::Camera*)>(0x00485A50)(m_base.m_camera);
    if (m_anim)
        StepAsset(m_anim, from, m_timeline.m_current, m_base.m_camera, false);
    PlaceCamera();
}

// 0x00587A50 (a later batch)
void CCameraAnimMovie::PlaceCamera() { GameFunction<void(__thiscall*)(CCameraAnimMovie*)>(0x00587A50)(this); }

} // namespace Obj

REPLACE(0x005871B0, &Obj::CObjectAnimMovie::Construct);
REPLACE(0x00587210, &Obj::CObjectAnimMovie::DestroyBody);
REPLACE(0x00587270, &Obj::CObjectAnimMovie::Start);
REPLACE(0x00587280, &Obj::CObjectAnimMovie::IsDone);
REPLACE(0x005872A0, &Obj::CObjectAnimMovie::IsHeldAndComplete);
REPLACE(0x005872C0, &Obj::CCameraAnimMovie::DestroyBody);
REPLACE(0x00587320, &Obj::CCameraAnimMovie::Start);
REPLACE(0x00587330, &Obj::CCameraAnimMovie::IsDone);
REPLACE(0x00587350, &Obj::CCameraAnimMovie::IsHeldAndComplete);
REPLACE(0x00587370, &Obj::CObjectAnimMovie::Destroy);
REPLACE(0x005873A0, &Obj::CObjectAnimMovie::Play);
REPLACE(0x005873F0, &Obj::CCameraAnimMovie::Construct);
REPLACE(0x00587450, &Obj::CCameraAnimMovie::Destroy);
REPLACE(0x00587480, &Obj::CCameraAnimMovie::LoadTarget);
REPLACE(0x00587540, &Obj::CCameraAnimMovie::Play);
REPLACE(0x00587590, &Obj::CCameraAnimMovie::CallMemberFunction);
REPLACE(0x005875D0, &Obj::CObjectAnimMovie::Load);
REPLACE(0x00587F70, &Obj::CCameraAnimMovie::Load);
REPLACE(0x00588090, &Obj::CCameraAnimMovie::Update);
