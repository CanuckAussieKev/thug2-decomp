// The object and camera animation movies, 0x005871B0-0x005880DF. See anim_movies.h.

#include "game/obj/anim_movies.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "game/ass/resource_registry.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/boned_anim_frame_data.h"
#include "game/gfx/view_cam.h"
#include "game/mth/mat4.h"
#include "game/mth/vec4.h"
#include "game/obj/cutscene.h"
#include "game/obj/node_objects.h"
#include "game/obj/entity.h"
#include "game/obj/entity_hub.h"
#include "game/script/names.h"
#include "game/script/param_struct.h"
#include "game/obj/compositemgr.h"
#include "game/core/code_address.h"
#include "game/front/front_end.h"
#include "game/gfx/viewport.h"
#include "game/gfx/viewport.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"

namespace GameObjects {

float g_animMoviesOne{1.0f};
float g_animMoviesSecondsPerFrame{0.016666668f};
float g_floatZero;

namespace {

extern void** const kObjectAnimMovieVtable;
extern void** const kCameraAnimMovieVtable;
constexpr uint32_t kNoAsset = 0xEE721991;   // a timer, not an asset
constexpr uint32_t kFrames = 0x019176C5;    // its length in frames
constexpr uint32_t kLooping = 0x5EA0E211;
constexpr uint32_t kBackwards = 0xF8CFD515;

using Gfx2D::CAnimTimeline;

// The base movie's code (CVideoEntry, node_objects.h) and the base's InvokeMethodByName (0x004A0CF0).
CVideoEntry* Base(void* movie) { return static_cast<CVideoEntry*>(movie); }
void BaseConstruct(void* movie) { Base(movie)->MakeNew(); }
void BaseDestroyBody(void* movie) { Base(movie)->DestroyBody(); }
void BaseLoad(void* movie, Scripts::CStruct* params) { Base(movie)->ConfigureFromData(params); }
void SetHeld(void* movie, int value) { Base(movie)->SetFlag1B(static_cast<uint8_t>(value)); }
// The movie's name (movie_manager.h).
uint32_t NameOf(void* movie) { return MovieName(movie); }
// The asset manager's asset by name (null if none).
void* FindAsset(uint32_t name) { return AssetStore::g_assetManager->GetAssetData(name, 0); }
// The movie's own Start (vtable slot 3).
void StartMovie(CMovie* movie) { reinterpret_cast<bool(__thiscall*)(CMovie*)>(movie->Vtable()[3])(movie); }

// The timeline's code: constructor / destructor body, complete, PlayAnim (slot 3), Update (slot 2).
void ConstructTimeline(CAnimTimeline* t) { t->MakeNew(); }
void DestroyTimeline(CAnimTimeline* t) { t->DestroyBody(); }
bool IsAccomplished(CAnimTimeline* t) { return t->IsAccomplished(); }
void PlayAnim(CAnimTimeline* t, uint32_t anim, float start, float end, int loopType, float blend, float speed)
{
    reinterpret_cast<void(__thiscall*)(CAnimTimeline*, uint32_t, float, float, int, float, float)>(t->m_vtable[3])(
        t, anim, start, end, loopType, blend, speed);
}
void UpdateTimeline(CAnimTimeline* t) { reinterpret_cast<void(__thiscall*)(CAnimTimeline*)>(t->m_vtable[2])(t); }

// The animation asset's code: started (0x00482F10); told a step from one time to another (0x00482F40).
void StartAsset(void* anim) { static_cast<Gfx2D::CJointClipFrames*>(anim)->ArmEvents(); }
void StepAsset(void* anim, float from, float to, Gfx2D::ViewCam* camera, bool first)
{
    static_cast<Gfx2D::CJointClipFrames*>(anim)->FireEvents(from, to, camera, first);
}

float SecondsPerFrame() { return g_animMoviesSecondsPerFrame; } // 1/60

constexpr uint32_t kFixedTarget = 0xC588EEBC; // a camera target that is a point (the target offset), not an object

template <typename T>
T& Prop(void* base, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset);
}

// 0x00463AA0 (cdecl, cutscene.h): a rotation matrix from a quaternion (negated in place on the way) with `origin` as
// its last row.
void QuatToMatrix(float* quat, float* origin, float* matrix) { GameObjects::QuatToMatrix(quat, origin, matrix); }

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
bool LoadAnim(Movie* movie, Scripts::CStruct* params)
{
    if (!params->HasBareFlag(kNoAsset)) {
        void* anim = FindAsset(NameOf(movie));
        if (!anim)
            return false;
        bool backwards = params->HasBareFlag(kBackwards);
        int loopType = params->HasBareFlag(kLooping) ? 1 : 0;
        movie->Perform(anim, loopType, backwards);
    } else {
        int frames = 0;
        params->FetchIntValue(kFrames, &frames, 1);
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
CObjectAnimMovie* CObjectAnimMovie::MakeNew()
{
    BaseConstruct(this);
    m_base.m_listLink.vtable = kObjectAnimMovieVtable;
    ConstructTimeline(&m_timeline);
    m_anim = nullptr;
    return this;
}

// 0x00587210
void CObjectAnimMovie::DestroyBody()
{
    m_base.m_listLink.vtable = kObjectAnimMovieVtable;
    DestroyTimeline(&m_timeline);
    BaseDestroyBody(this);
}

// 0x00587370
CObjectAnimMovie* CObjectAnimMovie::Teardown(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x005875D0
bool CObjectAnimMovie::ReadIn(Scripts::CStruct* params)
{
    BaseLoad(this, params);
    if (!LoadAnim(this, params))
        return false;
    StartMovie(&m_base);
    return true;
}

// 0x005873A0
void CObjectAnimMovie::Perform(void* anim, int loopType, bool backwards)
{
    m_anim = anim;
    PlayOn(&m_timeline, anim, loopType, backwards);
}

// 0x00587270
bool CObjectAnimMovie::Kickoff()
{
    if (m_anim)
        StartAsset(m_anim);
    return true;
}

// 0x00587280 / 0x005872A0
bool CObjectAnimMovie::IsFinished()
{
    if (m_base.m_unknown1A)
        return true;
    if (m_base.m_unknown18)
        return false;
    return IsAccomplished(&m_timeline);
}
int CObjectAnimMovie::IsHeldAndComplete() { return m_base.m_unknown18 && IsAccomplished(&m_timeline) ? 1 : 0; }

// --- the camera animation movie -------------------------------------------------------------------------------

// 0x005873F0
CCameraAnimMovie* CCameraAnimMovie::MakeNew()
{
    BaseConstruct(this);
    m_base.m_listLink.vtable = kCameraAnimMovieVtable;
    ConstructTimeline(&m_timeline);
    m_anim = nullptr;
    m_hasGoal = false;
    m_aimPoint = 0;
    reinterpret_cast<uint32_t&>(m_viewAngle) = 0;
    return this;
}

// 0x005872C0
void CCameraAnimMovie::DestroyBody()
{
    m_base.m_listLink.vtable = kCameraAnimMovieVtable;
    DestroyTimeline(&m_timeline);
    BaseDestroyBody(this);
}

// 0x00587450
CCameraAnimMovie* CCameraAnimMovie::Teardown(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00587F70
bool CCameraAnimMovie::ReadIn(Scripts::CStruct* params)
{
    BaseLoad(this, params);
    float fov;
    if (params->FetchFloat(0x17E7321E, &fov, 0)) {
        m_viewAngle = fov;
        m_base.m_viewer->ChangeHFov(fov);
    }
    if (!LoadAnim(this, params))
        return false;
    LoadTarget(params);
    StartMovie(&m_base);
    PlaceCamera();
    if (m_anim)
        StepAsset(m_anim, m_timeline.m_ongoing, m_timeline.m_ongoing, m_base.m_viewer, true);
    return true;
}

// 0x00587480
void CCameraAnimMovie::LoadTarget(Scripts::CStruct* params)
{
    m_hasGoal = params->ReadHashValue(0x531E4D28, &m_aimPoint, 0);
    if (!m_hasGoal)
        return;
    m_goalShift[0] = m_goalShift[1] = m_goalShift[2] = 0.0f;
    m_goalShift[3] = 1.0f;
    params->FetchVec(0x906642BF, reinterpret_cast<Scripts::VectorValue*>(m_goalShift), 0);
    float zero = g_floatZero;
    // (not equal, or unordered: a NaN counts as set)
    m_targetOffsetSet = !(m_goalShift[0] == zero) || !(m_goalShift[1] == zero) || !(m_goalShift[2] == zero);
    m_lookOffset[0] = m_lookOffset[1] = m_lookOffset[2] = 0.0f;
    m_lookOffset[3] = 1.0f;
    m_lookOffsetSet = params->FetchVec(0xA00D3D6E, reinterpret_cast<Scripts::VectorValue*>(m_lookOffset), 0);
}

// 0x00587540
void CCameraAnimMovie::Perform(void* anim, int loopType, bool backwards)
{
    m_anim = anim;
    PlayOn(&m_timeline, anim, loopType, backwards);
}

// 0x00587320
bool CCameraAnimMovie::Kickoff()
{
    if (m_anim)
        StartAsset(m_anim);
    return true;
}

// 0x00587330 / 0x00587350
bool CCameraAnimMovie::IsFinished()
{
    if (m_base.m_unknown1A)
        return true;
    if (m_base.m_unknown18)
        return false;
    return IsAccomplished(&m_timeline);
}
int CCameraAnimMovie::IsHeldAndComplete() { return m_base.m_unknown18 && IsAccomplished(&m_timeline) ? 1 : 0; }

// 0x00587590
bool CCameraAnimMovie::InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript* script)
{
    if (name == 0xA8D5A188) {
        LoadTarget(params);
        return true;
    }
    if (name == 0xD7241A6C) {
        m_hasGoal = false;
        m_aimPoint = 0;
        return true;
    }
    // (the base class's InvokeMethodByName: a stub shared by many vtables, 0x004A0CF0 - "not handled")
    return reinterpret_cast<bool(__thiscall*)(void*, uint32_t, Scripts::CStruct*, Scripts::CScript*)>(
        CodeAddress(&Engine3D::CViewRegion::QueryPoint))(this, name, params, script);
}

// 0x00588090
void CCameraAnimMovie::TickFrame()
{
    float from = m_timeline.m_ongoing;
    UpdateTimeline(&m_timeline);
    m_base.m_viewer->RememberPrevPos();
    if (m_anim)
        StepAsset(m_anim, from, m_timeline.m_ongoing, m_base.m_viewer, false);
    PlaceCamera();
}

// 0x00587A50
void CCameraAnimMovie::PlaceCamera()
{
    float* pos = reinterpret_cast<float*>(m_base.m_viewer->FetchLocation());
    float* m = reinterpret_cast<float*>(m_base.m_viewer->FetchMat());
    // A skater (of the two) whose 0x40 flag is up: the base's 0x00468F20(1).
    uint8_t** skaters = reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(GameModules::g_frontEndInstance) + 0x20);
    for (int i = 0; i < 2; ++i) {
        if (Prop<uint8_t>(Prop<uint8_t*>(skaters[i], 0x18), 0x20) & 0x40)
            Base(this)->CheckDone(true);
    }
    if (m_anim) {
        // The camera from the asset's frame: its position and its rotation.
        float quat[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        float at[4];
        bool flag = false;
        static_cast<Gfx2D::CJointClipFrames*>(m_anim)->GetFloatKeyPose(quat, at, &flag, m_timeline.m_ongoing, 0);
        for (int i = 0; i < 4; ++i)
            pos[i] = at[i];
        float origin[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        QuatToMatrix(quat, origin, m);
    } else {
        // Where it is; the field of view (0: the script global's).
        pos[0] = pos[1] = pos[2] = 0.0f;
        pos[3] = 1.0f;
        SetIdentity(m);
        float fov = m_viewAngle;
        if (fov == g_floatZero)
            fov = Scripts::FetchFloat(0x99529205);
        m_base.m_viewer->ChangeHFov(fov);
    }
    if (!m_hasGoal)
        return;
    float dir[4];
    if (m_aimPoint != kFixedTarget) {
        // Aimed at an object (none: as it was): from its position (moved by the offsets in its frame).
        auto* object = reinterpret_cast<uint8_t*>(reinterpret_cast<CObjectHubBase*>(g_compositeObjectManager)->LookUpEntity(m_aimPoint));
        if (!object)
            return;
        float target[4] = {Prop<float>(object, 0x4C), Prop<float>(object, 0x50), Prop<float>(object, 0x54), 1.0f};
        float frame[16];
        auto moved = [&](float* offset) {
            *reinterpret_cast<Maths::Mat4*>(frame) = *reinterpret_cast<const Maths::Mat4*>(object + 0x6C);
            frame[12] = target[0];
            frame[13] = target[1];
            frame[14] = target[2];
            frame[15] = 1.0f;
            reinterpret_cast<Maths::Mat4*>(frame)->OffsetLocal(*reinterpret_cast<const Maths::Vec4*>(offset));
        };
        if (m_targetOffsetSet) {
            moved(m_goalShift);
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
        reinterpret_cast<Maths::Vec4*>(dir)->MakeUnit();
        SetIdentity(m);
        for (int i = 0; i < 4; ++i)
            m[8 + i] = dir[i];
        CrossInto(m, m + 4, m + 8); // the right axis: up x at
        m[3] = 0.0f;
        reinterpret_cast<Maths::Vec4*>(m)->MakeUnit();
    } else {
        // A fixed point (the target offset), seen from the look offset.
        for (int i = 0; i < 4; ++i)
            pos[i] = m_lookOffset[i];
        // (x and y stay unrounded; z is stored and squared as its unrounded value times the stored one)
        double x = static_cast<double>(pos[0]) - m_goalShift[0];
        double y = static_cast<double>(pos[1]) - m_goalShift[1];
        double zExact = static_cast<double>(pos[2]) - m_goalShift[2];
        float z = static_cast<float>(zExact);
        double length = std::sqrt(zExact * z + y * y + x * x);
        if (length > g_floatZero) {
            double inverse = g_animMoviesOne / length;
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
        if (size > g_floatZero) {
            double inverse = g_animMoviesOne / size;
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
void CObjectAnimMovie::TickFrame()
{
    float from = m_timeline.m_ongoing;
    UpdateTimeline(&m_timeline);
    if (m_anim)
        StepAsset(m_anim, from, m_timeline.m_ongoing, m_base.m_viewer, false);
    // Every object the asset moves: its frame's rotation and position.
    int count = Prop<int>(m_anim, 8);
    auto* quats = static_cast<float*>(MemSys::Alloc(count * 16));
    if (quats) {
        for (int i = 0; i < count; ++i) {
            quats[4 * i] = quats[4 * i + 1] = quats[4 * i + 2] = 0.0f;
            quats[4 * i + 3] = 1.0f;
        }
    }
    auto* positions = static_cast<float*>(MemSys::Alloc(Prop<int>(m_anim, 8) * 16));
    auto* flags = static_cast<bool*>(MemSys::Alloc(Prop<int>(m_anim, 8)));
    static_cast<Gfx2D::CJointClipFrames*>(m_anim)->GetFloatKeyPose(quats, positions, flags, m_timeline.m_ongoing, 0);
    for (int i = 0; i < Prop<int>(m_anim, 8); ++i) {
        uint32_t name = static_cast<Gfx2D::CJointClipFrames*>(m_anim)->FetchJointName(i);
        uint8_t* object = reinterpret_cast<uint8_t*>(GameObjects::FindObject(name));
        if (!object) {
            DebugOut::Printf("object animation:  couldn't find object %s\n", Scripts::FindChecksumNameOrUnknown(name));
            continue;
        }
        float origin[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        float matrix[16];
        QuatToMatrix(quats + 4 * i, origin, matrix);
        std::memcpy(object + 0x6C, matrix, sizeof(matrix)); // its matrix
        std::memcpy(object + 0xC0, matrix, sizeof(matrix)); // and its display matrix
        std::memcpy(object + 0x4C, positions + 4 * i, 16);  // its position
        if (Scripts::FetchIntValue(0x2F510F45)) {
            DebugOut::Printf("Object %s @ time %f:  ", Scripts::FindChecksumNameOrUnknown(name),
                        static_cast<double>(m_timeline.m_ongoing));
            reinterpret_cast<const Maths::Vec4*>(object + 0x4C)->EchoOut(); // (printed)
            reinterpret_cast<const Maths::Mat4*>(object + 0x6C)->EchoOut();
        }
        // The object's model told (slot 1 of its 0x286A8D26 component).
        void* component = reinterpret_cast<CGameEntity*>(object)->FetchModule(0x286A8D26);
        if (component)
            reinterpret_cast<void(__thiscall*)(void*)>((*static_cast<void***>(component))[1])(component);
    }
    MemSys::Hub::Pull()->Dealloc(quats);
    MemSys::Hub::Pull()->Dealloc(positions);
    MemSys::Hub::Pull()->Dealloc(flags);
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kObjectAnimMovieVtable_slots[] = {&GameObjects::CObjectAnimMovie::Teardown,
         &GameObjects::CObjectAnimMovie::ReadIn,
         &GameObjects::CObjectAnimMovie::TickFrame,
         &GameObjects::CObjectAnimMovie::Kickoff,
         &Engine3D::CViewRegion::QueryPoint,
         &GameObjects::CObjectAnimMovie::IsFinished,
         &GameObjects::CObjectAnimMovie::IsHeldAndComplete,
         Stubs::ReturnFalse,
         Stubs::DoNothing,
         Stubs::ReturnTrue,
         Stubs::ReturnFalse,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         Stubs::DoNothing};
void** const kObjectAnimMovieVtable = reinterpret_cast<void**>(kObjectAnimMovieVtable_slots);
static VtableSlot kCameraAnimMovieVtable_slots[] = {&GameObjects::CCameraAnimMovie::Teardown,
         &GameObjects::CCameraAnimMovie::ReadIn,
         &GameObjects::CCameraAnimMovie::TickFrame,
         &GameObjects::CCameraAnimMovie::Kickoff,
         &GameObjects::CCameraAnimMovie::InvokeMethodByName,
         &GameObjects::CCameraAnimMovie::IsFinished,
         &GameObjects::CCameraAnimMovie::IsHeldAndComplete,
         Stubs::ReturnTrue,
         Stubs::DoNothing,
         Stubs::ReturnTrue,
         Stubs::ReturnTrue,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         Stubs::DoNothing};
void** const kCameraAnimMovieVtable = reinterpret_cast<void**>(kCameraAnimMovieVtable_slots);

} // namespace
} // namespace Obj
