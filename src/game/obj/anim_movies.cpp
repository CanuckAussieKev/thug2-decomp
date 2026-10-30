// The object and camera animation movies, 0x005871B0-0x005880DF. See anim_movies.h.

#include "game/obj/anim_movies.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "game/ass/assman.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/boned_anim_frame_data.h"
#include "game/gfx/camera.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/cutscene.h"
#include "game/obj/node_objects.h"
#include "game/obj/object.h"
#include "game/obj/objman.h"
#include "game/script/names.h"
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

// The base movie's code (CMovieDetails, node_objects.h) and the base's CallMemberFunction (0x004A0CF0).
CMovieDetails* Base(void* movie) { return static_cast<CMovieDetails*>(movie); }
void BaseConstruct(void* movie) { Base(movie)->Construct(); }
void BaseDestroyBody(void* movie) { Base(movie)->DestroyBody(); }
void BaseLoad(void* movie, Script::CStruct* params) { Base(movie)->InitFromStructure(params); }
void SetHeld(void* movie, int value) { Base(movie)->SetFlag1B(static_cast<uint8_t>(value)); }
// The movie's name (movie_manager.h).
uint32_t NameOf(void* movie) { return MovieName(movie); }
// The asset manager's asset by name (null if none).
void* FindAsset(uint32_t name) { return GameVariable<Ass::CAssMan*>(0x006918DC)->GetAssetData(name, 0); }
// The movie's own Start (vtable slot 3).
void StartMovie(CMovie* movie) { reinterpret_cast<bool(__thiscall*)(CMovie*)>(movie->Vtable()[3])(movie); }

// The timeline's code: constructor / destructor body, complete, PlayAnim (slot 3), Update (slot 2).
void ConstructTimeline(CAnimTimeline* t) { t->Construct(); }
void DestroyTimeline(CAnimTimeline* t) { t->DestroyBody(); }
bool IsComplete(CAnimTimeline* t) { return t->IsComplete(); }
void PlayAnim(CAnimTimeline* t, uint32_t anim, float start, float end, int loopType, float blend, float speed)
{
    reinterpret_cast<void(__thiscall*)(CAnimTimeline*, uint32_t, float, float, int, float, float)>(t->m_vtable[3])(
        t, anim, start, end, loopType, blend, speed);
}
void UpdateTimeline(CAnimTimeline* t) { reinterpret_cast<void(__thiscall*)(CAnimTimeline*)>(t->m_vtable[2])(t); }

// The animation asset's code: started (0x00482F10); told a step from one time to another (0x00482F40).
void StartAsset(void* anim) { static_cast<Gfx::CBonedAnimFrameData*>(anim)->ArmEvents(); }
void StepAsset(void* anim, float from, float to, Gfx::Camera* camera, bool first)
{
    static_cast<Gfx::CBonedAnimFrameData*>(anim)->FireEvents(from, to, camera, first);
}

float SecondsPerFrame() { return GameVariable<float>(0x006463B8); } // 1/60

constexpr uint32_t kFixedTarget = 0xC588EEBC; // a camera target that is a point (the target offset), not an object

template <typename T>
T& Field(void* base, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset);
}

// 0x00463AA0 (cdecl, cutscene.h): a rotation matrix from a quaternion (negated in place on the way) with `origin` as
// its last row.
void QuatToMatrix(float* quat, float* origin, float* matrix) { Obj::QuatToMatrix(quat, origin, matrix); }

void SetIdentity(float* m)
{
    for (int i = 0; i < 16; ++i)
        m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
}

// out = a x b (x, y and z; each a difference of products, rounded once).
void CrossInto(float* out, const float* a, const float* b)
{
    double x = static_cast<double>(a[1]) * b[2] - static_cast<double>(a[2]) * b[1];
    double y = static_cast<double>(a[2]) * b[0] - static_cast<double>(a[0]) * b[2];
    double z = static_cast<double>(a[0]) * b[1] - static_cast<double>(a[1]) * b[0];
    out[0] = static_cast<float>(x);
    out[1] = static_cast<float>(y);
    out[2] = static_cast<float>(z);
}

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
        m_base.m_camera->SetHFOV(fov);
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
    m_base.m_camera->StoreOldPos();
    if (m_anim)
        StepAsset(m_anim, from, m_timeline.m_current, m_base.m_camera, false);
    PlaceCamera();
}

// 0x00587A50
void CCameraAnimMovie::PlaceCamera()
{
    float* pos = reinterpret_cast<float*>(m_base.m_camera->GetPos());
    float* m = reinterpret_cast<float*>(m_base.m_camera->GetMatrix());
    // A skater (of the two) whose 0x40 flag is up: the base's 0x00468F20(1).
    uint8_t** skaters = reinterpret_cast<uint8_t**>(GameVariable<uint8_t*>(0x007CE464) + 0x20);
    for (int i = 0; i < 2; ++i) {
        if (Field<uint8_t>(Field<uint8_t*>(skaters[i], 0x18), 0x20) & 0x40)
            Base(this)->CheckDone(true);
    }
    if (m_anim) {
        // The camera from the asset's frame: its position and its rotation.
        float quat[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        float at[4];
        bool flag = false;
        static_cast<Gfx::CBonedAnimFrameData*>(m_anim)->GetFloatKeyPose(quat, at, &flag, m_timeline.m_current, 0);
        for (int i = 0; i < 4; ++i)
            pos[i] = at[i];
        float origin[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        QuatToMatrix(quat, origin, m);
    } else {
        // Where it is; the field of view (0: the script global's).
        pos[0] = pos[1] = pos[2] = 0.0f;
        pos[3] = 1.0f;
        SetIdentity(m);
        float fov = m_fov;
        if (fov == GameVariable<float>(0x0066D520))
            fov = Script::GetFloat(0x99529205);
        m_base.m_camera->SetHFOV(fov);
    }
    if (!m_hasTarget)
        return;
    float dir[4];
    if (m_target != kFixedTarget) {
        // Aimed at an object (none: as it was): from its position (moved by the offsets in its frame).
        auto* object = reinterpret_cast<uint8_t*>(GameVariable<CBaseManager*>(0x006F0FB8)->GetObjectByID(m_target));
        if (!object)
            return;
        float target[4] = {Field<float>(object, 0x4C), Field<float>(object, 0x50), Field<float>(object, 0x54), 1.0f};
        float frame[16];
        auto moved = [&](float* offset) {
            *reinterpret_cast<Mth::Matrix*>(frame) = *reinterpret_cast<const Mth::Matrix*>(object + 0x6C);
            frame[12] = target[0];
            frame[13] = target[1];
            frame[14] = target[2];
            frame[15] = 1.0f;
            reinterpret_cast<Mth::Matrix*>(frame)->TranslateLocal(*reinterpret_cast<const Mth::Vector*>(offset));
        };
        if (m_targetOffsetSet) {
            moved(m_targetOffset);
            for (int i = 0; i < 4; ++i)
                target[i] = frame[12 + i];
        }
        if (m_lookOffsetSet) {
            for (int i = 0; i < 4; ++i)
                pos[i] = target[i];
            moved(m_lookOffset);
            for (int i = 0; i < 4; ++i)
                pos[i] = frame[12 + i];
        }
        for (int i = 0; i < 4; ++i)
            dir[i] = pos[i] - target[i];
        reinterpret_cast<Mth::Vector*>(dir)->Normalize();
        SetIdentity(m);
        for (int i = 0; i < 4; ++i)
            m[8 + i] = dir[i];
        CrossInto(m, m + 4, m + 8); // the right axis: up x at
        m[3] = 0.0f;
        reinterpret_cast<Mth::Vector*>(m)->Normalize();
    } else {
        // A fixed point (the target offset), seen from the look offset.
        for (int i = 0; i < 4; ++i)
            pos[i] = m_lookOffset[i];
        // (x and y stay unrounded; z is stored and squared as its unrounded value times the stored one)
        double x = static_cast<double>(pos[0]) - m_targetOffset[0];
        double y = static_cast<double>(pos[1]) - m_targetOffset[1];
        double zExact = static_cast<double>(pos[2]) - m_targetOffset[2];
        float z = static_cast<float>(zExact);
        double length = std::sqrt(zExact * z + y * y + x * x);
        if (length > GameVariable<float>(0x0066D520)) {
            double inverse = GameVariable<float>(0x006453A8) / length;
            x *= inverse;
            y *= inverse;
            z = static_cast<float>(z * inverse);
        }
        SetIdentity(m);
        m[8] = static_cast<float>(x);
        m[10] = z;
        m[9] = static_cast<float>(y);
        m[11] = 0.0f;
        CrossInto(m, m + 4, m + 8);
        m[3] = 0.0f;
        double a = m[0], b = m[1], c = m[2];
        double size = std::sqrt(a * a + b * b + c * c);
        if (size > GameVariable<float>(0x0066D520)) {
            double inverse = GameVariable<float>(0x006453A8) / size;
            m[0] = static_cast<float>(inverse * m[0]);
            m[1] = static_cast<float>(inverse * m[1]);
            m[2] = static_cast<float>(m[2] * inverse);
        }
    }
    // The up axis: at x right.
    CrossInto(m + 4, m + 8, m);
    m[7] = 0.0f;
}

// 0x005876A0 (and the thunk 0x00587F60)
void CObjectAnimMovie::Update()
{
    float from = m_timeline.m_current;
    UpdateTimeline(&m_timeline);
    if (m_anim)
        StepAsset(m_anim, from, m_timeline.m_current, m_base.m_camera, false);
    // Every object the asset moves: its frame's rotation and position.
    int count = Field<int>(m_anim, 8);
    auto* quats = static_cast<float*>(Mem::New(count * 16));
    if (quats) {
        for (int i = 0; i < count; ++i) {
            quats[4 * i] = quats[4 * i + 1] = quats[4 * i + 2] = 0.0f;
            quats[4 * i + 3] = 1.0f;
        }
    }
    auto* positions = static_cast<float*>(Mem::New(Field<int>(m_anim, 8) * 16));
    auto* flags = static_cast<bool*>(Mem::New(Field<int>(m_anim, 8)));
    static_cast<Gfx::CBonedAnimFrameData*>(m_anim)->GetFloatKeyPose(quats, positions, flags, m_timeline.m_current, 0);
    for (int i = 0; i < Field<int>(m_anim, 8); ++i) {
        uint32_t name = static_cast<Gfx::CBonedAnimFrameData*>(m_anim)->GetBoneName(i);
        uint8_t* object = reinterpret_cast<uint8_t*>(Obj::FindObject(name));
        if (!object) {
            Dbg::Printf(reinterpret_cast<const char*>(0x0064F058), Script::FindChecksumNameOrUnknown(name));
            continue;
        }
        float origin[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        float matrix[16];
        QuatToMatrix(quats + 4 * i, origin, matrix);
        std::memcpy(object + 0x6C, matrix, sizeof(matrix)); // its matrix
        std::memcpy(object + 0xC0, matrix, sizeof(matrix)); // and its display matrix
        std::memcpy(object + 0x4C, positions + 4 * i, 16);  // its position
        if (Script::GetInteger(0x2F510F45)) {
            Dbg::Printf(reinterpret_cast<const char*>(0x0064F084), Script::FindChecksumNameOrUnknown(name),
                        static_cast<double>(m_timeline.m_current));
            reinterpret_cast<const Mth::Vector*>(object + 0x4C)->Print(); // (printed)
            reinterpret_cast<const Mth::Matrix*>(object + 0x6C)->Print();
        }
        // The object's model told (slot 1 of its 0x286A8D26 component).
        void* component = reinterpret_cast<CCompositeObject*>(object)->GetComponent(0x286A8D26);
        if (component)
            reinterpret_cast<void(__thiscall*)(void*)>((*static_cast<void***>(component))[1])(component);
    }
    Mem::Manager::Get()->Free(quats);
    Mem::Manager::Get()->Free(positions);
    Mem::Manager::Get()->Free(flags);
}

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
REPLACE(0x005876A0, &Obj::CObjectAnimMovie::Update);
REPLACE(0x00587A50, &Obj::CCameraAnimMovie::PlaceCamera);
REPLACE(0x00587F60, &Obj::CObjectAnimMovie::Update); // (a jump to 0x005876A0)
