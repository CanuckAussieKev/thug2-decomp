// Self-tests for the skater camera component and the skater's cleanup-state component (skater_camera_component.cpp,
// skater_camera_update.cpp): original vs ours on a stand-in camera, skater and the components they reach, the game
// calls stood in for (skater_core_testkit.h); the vector and matrix maths the original calls pointed at ours.

#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/camera.h"
#include "game/mth/matrix.h"
#include "game/mth/slerp.h"
#include "game/mth/vector.h"
#include "game/nx/feeler.h"
#include "game/obj/camera_util.h"
#include "game/obj/object.h"
#include "game/obj/objman.h"
#include "game/obj/smtptr.h"
#include "game/script/struct.h"
#include "game/skater/skater.h"
#include "game/skater/skater_camera_component.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/skater_core_testkit.h"

namespace {

using namespace SkaterCoreKit;
using Obj::CSkaterCameraComponent;
using Obj::CSkaterCleanupStateComponent;

// Where things are in the arena (the camera component at 0).
constexpr uint32_t kCam = 0x0000;          // the skater camera component (0x17C bytes)
constexpr uint32_t kCamObject = 0x0200;    // its object (the camera: +0x4C position, +0x6C matrix)
constexpr uint32_t kSkaterObj = 0x0400;    // the skater (+0x24 flags, +0x4C, +0x6C, +0xAC, +0xBC, +0xC0, +0x104, +0x280)
constexpr uint32_t kStateComp = 0x0800;    // its SkaterState (bytes, +0x18 the state, +0xE4 / +0xF8 / +0x108 vectors)
constexpr uint32_t kControlComp = 0x0940;  // its SkaterPhysicsControl (+0x18 paused, +0x2C -> kDriveState)
constexpr uint32_t kDriveState = 0x0980;   // (+0x118 driving)
constexpr uint32_t kLookComp = 0x0AA0;     // the CameraLookAround (+0x18, +0x19, +0x1C, +0x20, +0x24, +0x28)
constexpr uint32_t kSlerp = 0x0B00;        // the interpolator (0x98)
constexpr uint32_t kCameraComp = 0x0BC0;   // the Camera component (+0xC the camera object, +0x18 the Gfx camera)
constexpr uint32_t kCorePhys = 0x0C00;     // the skater's core physics (+0x18 its display matrix, +0x2EC, +0x3B0, +0x152C)
constexpr uint32_t kMovable = 0x3000;      // its movable contact (+0x18 a ride)
constexpr uint32_t kBalanceComp = 0x3040;  // the balance trick (+0xAC)
constexpr uint32_t kOut = 0x3100;          // outputs (state, vectors, floats)
constexpr uint32_t kSkaterExtra = 0x3200;  // what the skater's +0x280 points at (+0x18 a matrix)

int g_coreAsks;

float PickCam()
{
    static const float values[] = {0.0f, 0.5f, 1.0f, 2.5f, -1.5f, 0.016f, 0.25f, 0.001f, -0.25f, 7.0f, 0.05f, 0.9f,
                                   0.3f, 2.0f, 0.7f, -0.7f, 0.1f, 0.02f};
    return values[Next() % (sizeof(values) / sizeof(values[0]))];
}

uint32_t ComponentFor(uint32_t type)
{
    switch (type) {
    case 0xC4E311FA: return reinterpret_cast<uint32_t>(ArenaPtr(kCameraComp));
    case 0x5BD63B29: return reinterpret_cast<uint32_t>(ArenaPtr(kCorePhys));
    case 0x589E5FB3: return reinterpret_cast<uint32_t>(ArenaPtr(kBalanceComp));
    case 0x43686585: return reinterpret_cast<uint32_t>(ArenaPtr(kStateComp));
    case 0xD9151D4D: return reinterpret_cast<uint32_t>(ArenaPtr(kControlComp));
    default: return 0x51000000u + (type & 0xFFF0);
    }
}

void RecordMatrix(const void* m)
{
    const float* f = static_cast<const float*>(m);
    for (int i = 0; i < 16; ++i)
        RecordFloat(f[i]);
}
void RecordVector(const void* v)
{
    const float* f = static_cast<const float*>(v);
    for (int i = 0; i < 4; ++i)
        RecordFloat(f[i]);
}

std::vector<StandIn> CameraStandIns()
{
    using Script::CStruct;
    return {
        {0x005BEAA0, 0, 0, Ret::Float, false, 0, {Ours(&Tmr::GetSlomoFrameSeconds)},
         [](Frame&, Result& r) { r.st0 = Next() % 2 ? 0.016f : 0.05f; }},
        {0x004E4340, 0, 2, Ret::Float, false, 0, {Ours(&Obj::FrameBlend)},
         [](Frame&, Result& r) { r.st0 = Next() % 3 ? 0.25f : 1.0f; }},
        {0x004F9970, 0, 0, Ret::None, true},
        {0x004F9AE0, 8, 2, Ret::None, true},
        {0x004F99F0, 0, 0, Ret::None, true},
        {0x0045DB90, 4, 1, Ret::Int, true, 0, {Ours(&Obj::CCompositeObject::GetComponent)},
         [](Frame& f, Result& r) {
             r.eax = ComponentFor(f.args[0]);
             // (The core physics missing only where the code allows it: the first ask, the wall-ride check.)
             if (f.args[0] == 0x5BD63B29 && g_coreAsks++ == 0 && Next() % 5 == 0)
                 r.eax = 0;
         }},
        {0x00485A50, 0, 0, Ret::None, true, 0, {Ours(&Gfx::Camera::StoreOldPos)}},
        {0x00405990, 8, 2, Ret::Int, true, 0, {Ours(&Mth::SlerpInterpolator::SetMatrices)},
         [](Frame& f, Result&) {
             RecordMatrix(reinterpret_cast<const void*>(f.args[0]));
             RecordMatrix(reinterpret_cast<const void*>(f.args[1]));
         }},
        {0x00405A20, 8, 2, Ret::None, true, 0, {Ours(&Mth::SlerpInterpolator::GetMatrix)},
         [](Frame& f, Result&) {
             auto* out = reinterpret_cast<float*>(f.args[0]);
             for (int i = 0; i < 16; ++i)
                 out[i] = out[i] * 0.5f + PickCam();
         }},
        {0x00478F60, 0, 1, Ret::Int, false, 0, {Ours(static_cast<int (*)(uint32_t)>(&Script::GetInteger))},
         [](Frame&, Result& r) { r.eax = Next() % 40; }},
        {0x00417710, 12, 3, Ret::Int, true, 0, {Ours(&Nx::CFeeler::GetCollisionAlong)},
         [](Frame& f, Result& r) {
             RecordVector(reinterpret_cast<const void*>(f.args[0]));
             RecordVector(reinterpret_cast<const void*>(f.args[1]));
             r.eax = Next() % 3 ? 1 : 0;
             reinterpret_cast<Nx::CFeeler*>(f.ecx)->m_distance = PickCam();
         }},
        {0x004637E0, 4, 1, Ret::None, true, 0, {Ours(&Obj::CRefCounted::RemoveSmartPointer)}},
        {0x004E4360, 0, 6, Ret::None, false, 0x30, {Ours(&Obj::AvoidCameraCollisions)},
         [](Frame& f, Result&) {
             RecordVector(reinterpret_cast<const void*>(f.args[0]));
             RecordMatrix(reinterpret_cast<const void*>(f.args[1]));
             RecordVector(reinterpret_cast<const void*>(f.args[2]));
             RecordVector(reinterpret_cast<const void*>(f.args[3]));
         }},
        {0x004E1950, 0, 0, Ret::Int, false, 0, {Ours(&Mem::EnsureManager)}},
        {0x005BAFC0, 12, 3, Ret::Int, true, 0, {Ours(&Mem::Manager::Allocate)},
         [](Frame&, Result& r) { r.eax = Next() % 4 ? reinterpret_cast<uint32_t>(ArenaPtr(kSlerp)) : 0; }},
        {0x005BABA0, 4, 1, Ret::None, true, 0, {Ours(&Mem::Manager::Free)}},
        {0x00401CC0, 0, 1, Ret::Int, false, 0, {Ours(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))},
         [](Frame&, Result& r) { r.eax = Next() % 4 ? reinterpret_cast<uint32_t>(ArenaPtr(kCam)) : 0; }},
        {0x0045D740, 0, 0, Ret::None, true, 0, {Ours(&Obj::CBaseComponent::DestroyBody)}},
        {0x005809C0, 8, 2, Ret::None, true, 0, {Ours(&Obj::CSkater::UpdateShadowLight)}},
        {0x004619F0, 4, 1, Ret::Int, true, 0, {Ours(&Obj::CBaseManager::GetObjectByID)},
         [](Frame&, Result& r) { r.eax = Next() % 4 ? reinterpret_cast<uint32_t>(ArenaPtr(kSkaterObj)) : 0; }},
        {0x00476EC0, 12, 3, Ret::Bool, true, 4,
         {Ours(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))},
         [](Frame& f, Result& r) {
             r.eax = 1;
             *reinterpret_cast<uint32_t*>(f.args[1]) = 0x1234u + Next() % 3;
         }},
        {0x00476CB0, 12, 3, Ret::Bool, true, 4,
         {Ours(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))},
         [](Frame& f, Result& r) {
             r.eax = 1;
             *reinterpret_cast<int*>(f.args[1]) = static_cast<int>(Next() % 6);
         }},
        {0x00476720, 12, 3, Ret::Bool, true, 0,
         {Ours(static_cast<bool (CStruct::*)(const char*, float*, int)>(&CStruct::GetFloat))},
         [](Frame& f, Result& r) {
             if (r.eax)
                 *reinterpret_cast<float*>(f.args[1]) = PickCam();
         }},
    };
}

void SetVector(uint32_t at, float x, float y, float z, float w) { ArenaAt<Mth::Vector>(at) = Mth::Vector(x, y, z, w); }
void Orthonormal(uint32_t at, float a)
{
    // An orientation turned `a` about y (rows right, up, at, then the position).
    float c = 1.0f - a * a * 0.5f, s = a;
    SetVector(at, c, 0.0f, -s, 0.0f);
    SetVector(at + 0x10, 0.0f, 1.0f, 0.0f, 0.0f);
    SetVector(at + 0x20, s, 0.0f, c, 0.0f);
    SetVector(at + 0x30, 1.0f, 2.0f, 3.0f, 1.0f);
}

void FillCamera()
{
    FillArena();
    std::memset(ArenaPtr(kOut), 0, 0x100);
    auto pointer = [](uint32_t at, uint32_t to) { ArenaAt<uint8_t*>(at) = ArenaPtr(to); };
    pointer(kCam + 0x0C, kCamObject);
    pointer(kCam + 0x168, kLookComp);
    pointer(kCam + 0x16C, kSlerp);
    pointer(kCam + 0x170, kSkaterObj);
    pointer(kCam + 0x174, kStateComp);
    pointer(kCam + 0x178, kControlComp);
    if (Next() % 6 == 0)
        ArenaAt<void*>(kCam + 0x178) = nullptr;
    if (Next() % 12 == 0)
        ArenaAt<void*>(kCam + 0x170) = nullptr;
    pointer(kControlComp + 0x2C, kDriveState);
    ArenaAt<uint8_t>(kDriveState + 0x118) = Next() % 8 == 0;
    ArenaAt<uint8_t>(kControlComp + 0x18) = Next() % 6 == 0;
    pointer(kCameraComp + 0x0C, kCamObject);
    ArenaAt<uint32_t>(kCameraComp + 0x18) = 0x5E000000u;
    pointer(kSkaterObj + 0x280, kSkaterExtra);
    pointer(kCorePhys + 0x152C, kMovable);
    ArenaAt<uint32_t>(kMovable + 0x18) = Next() % 2 ? 0x5F000000u : 0;
    ArenaAt<uint8_t>(kCorePhys + 0x2EC) = Next() % 4 == 0;
    // The state: which (0-5), its bytes.
    ArenaAt<int>(kStateComp + 0x18) = static_cast<int>(Next() % 5);
    for (uint32_t b : {0x30u, 0x48u, 0x78u, 0x98u, 0xA0u, 0xA8u, 0xE0u, 0xF4u})
        ArenaAt<uint8_t>(kStateComp + b) = RandomBool(40);
    ArenaAt<int>(kStateComp + 0x1C) = Next() % 3 == 0;
    SetVector(kStateComp + 0x108, 0.0f, 1.0f, 0.0f, 0.0f);
    if (Next() % 2)
        SetVector(kStateComp + 0x108, 0.28f, 0.96f, 0.0f, 0.0f);
    // The skater: flags, orientations, velocity (fast or nearly still).
    ArenaAt<uint32_t>(kSkaterObj + 0x104) = Next() % 5 == 0 ? 8u : 0u;
    ArenaAt<uint8_t>(kSkaterObj + 0xBC) = RandomBool(50);
    Orthonormal(kSkaterObj + 0x6C, 0.1f);
    Orthonormal(kSkaterObj + 0xC0, 0.2f);
    Orthonormal(kSkaterExtra + 0x18, -0.15f);
    Orthonormal(kCorePhys + 0x3B0, 0.05f);
    float scale = Next() % 3 ? 10.0f : 0.01f;
    SetVector(kSkaterObj + 0xAC, RandomFloat(1.0f) * scale, RandomFloat(1.0f) * scale, RandomFloat(1.0f) * scale, 0.0f);
    // The camera: its orientation near the skater's (aligned at times), the fields.
    Orthonormal(kCam + 0x40, Next() % 3 ? 0.0f : 0.3f);
    Orthonormal(kCam + 0x74, 0.25f);
    ArenaAt<int>(kCam + 0x24) = static_cast<int>(Next() % 7);
    ArenaAt<int>(kCam + 0x28) = static_cast<int>(Next() % 4);
    ArenaAt<int>(kCam + 0x70) = static_cast<int>(Next() % 3);
    ArenaAt<int>(kCam + 0x120) = static_cast<int>(Next() % 3);
    ArenaAt<uint8_t>(kCam + 0x104) = RandomBool(50);
    for (uint32_t f : {0x2Cu, 0xC4u, 0xD0u, 0xDCu, 0xE4u, 0xE8u, 0xECu, 0xF0u, 0xF4u, 0xF8u, 0xFCu, 0x100u, 0x108u, 0x10Cu,
                       0x110u, 0x114u, 0x118u, 0x11Cu, 0x124u, 0x128u, 0x12Cu, 0x130u, 0x134u, 0x138u, 0x140u, 0x144u,
                       0x148u, 0x150u, 0x154u, 0x158u, 0x15Cu, 0x160u, 0x164u})
        ArenaAt<float>(kCam + f) = PickCam();
    SetVector(kCam + 0xB4, 0.6f, 0.0f, 0.8f, 1.0f);
    // The look-around.
    ArenaAt<uint8_t>(kLookComp + 0x18) = RandomBool(30);
    ArenaAt<uint8_t>(kLookComp + 0x19) = RandomBool(30);
    ArenaAt<float>(kLookComp + 0x1C) = Next() % 2 ? 1.0f : 0.7f;
    for (uint32_t f : {0x20u, 0x24u, 0x28u})
        ArenaAt<float>(kLookComp + f) = PickCam();
    ArenaAt<float>(kBalanceComp + 0xAC) = PickCam();
}

void TestSkaterCamera()
{
    StandIns standIns(CameraStandIns());
    UseOurs maths({{0x004023F0, Ours(&Mth::Vector::Normalize)},
                   {0x00402340, Ours(&Mth::Vector::operator+=)},
                   {0x00402550, Ours(&Mth::CrossProduct)},
                   {0x004024A0, Ours(static_cast<Mth::Vector (*)(const Mth::Vector&, float)>(&Mth::operator*))},
                   {0x00402470, Ours(static_cast<Mth::Vector (*)(const Mth::Vector&, const Mth::Vector&)>(&Mth::operator-))},
                   {0x0041A6D0, Ours(&Mth::Matrix::RotateY)},
                   {0x00405710, Ours(&Mth::Matrix::RotateYLocal)},
                   {0x004048D0, Ours(&Mth::Matrix::SetFromEulerAngles)},
                   {0x00421440, Ours(&Mth::Vector::RotateAboutAxis)},
                   {0x00404E20, Ours(&Mth::CreateRotateYMatrix)},
                   {0x00404DD0, Ours(&Mth::CreateRotateXMatrix)}});
    bool savedReady = GameVariable<bool>(0x007CC979);
    float savedLipTurn = GameVariable<float>(0x007CCDB4);
    static bool ready;
    auto start = [] {
        g_coreAsks = 0;
        GameVariable<bool>(0x007CC979) = ready;
        GameVariable<float>(0x007CCDB4) = 0.0f;
    };
    auto finish = [](uint32_t result) {
        RecordFloat(GameVariable<float>(0x007CCDB4));
        return result;
    };
    using Run = std::function<uint32_t(uint8_t*)>;
    using C = CSkaterCameraComponent;
    using K = CSkaterCleanupStateComponent;
    struct Case {
        const char* name;
        Run original;
        Run ours;
    };
    auto self = [](uint8_t* c) { return reinterpret_cast<C*>(c); };
    auto cleanup = [](uint8_t* c) { return reinterpret_cast<K*>(c); };
    auto params = reinterpret_cast<Script::CStruct*>(0x5A5A0000);
    auto voidCase = [&](const char* name, uint32_t address, void (C::*ours)()) {
        return Case{name,
                    [=](uint8_t* c) {
                        start();
                        Original<void(__thiscall*)(void*)>(address)(c);
                        return finish(0);
                    },
                    [=](uint8_t* c) {
                        start();
                        (self(c)->*ours)();
                        return finish(0);
                    }};
    };
    auto cleanupCase = [&](const char* name, uint32_t address, void (K::*ours)()) {
        return Case{name,
                    [=](uint8_t* c) {
                        start();
                        Original<void(__thiscall*)(void*)>(address)(c);
                        return finish(0);
                    },
                    [=](uint8_t* c) {
                        start();
                        (cleanup(c)->*ours)();
                        return finish(0);
                    }};
    };
    static uint32_t memberName;
    const std::vector<Case> cases = {
        voidCase("Update", 0x004FA860, &C::Update),
        voidCase("Update", 0x004FA860, &C::Update),
        voidCase("Update", 0x004FA860, &C::Update),
        voidCase("Update", 0x004FA860, &C::Update),
        {"ApplyShake",
         [=](uint8_t* c) {
             start();
             Original<void(__thiscall*)(void*, void*, void*)>(0x004F9F00)(c, ArenaPtr(kOut), ArenaPtr(kSkaterObj + 0x6C));
             return finish(0);
         },
         [=](uint8_t* c) {
             start();
             self(c)->ApplyShake(&ArenaAt<Mth::Vector>(kOut), &ArenaAt<Mth::Matrix>(kSkaterObj + 0x6C));
             return finish(0);
         }},
        {"GetTarget",
         [=](uint8_t* c) {
             start();
             Original<void(__thiscall*)(void*, void*, bool)>(0x004FA020)(c, ArenaPtr(kOut), (Next() & 1) != 0);
             return finish(0);
         },
         [=](uint8_t* c) {
             start();
             self(c)->GetTarget(&ArenaAt<Mth::Vector>(kOut), (Next() & 1) != 0);
             return finish(0);
         }},
        {"GetZoomedOffsets",
         [=](uint8_t* c) {
             start();
             Original<void(__thiscall*)(void*, void*, void*)>(0x004FA230)(c, ArenaPtr(kOut), ArenaPtr(kOut + 4));
             return finish(0);
         },
         [=](uint8_t* c) {
             start();
             self(c)->GetZoomedOffsets(&ArenaAt<float>(kOut), &ArenaAt<float>(kOut + 4));
             return finish(0);
         }},
        {"SetSkater",
         [=](uint8_t* c) {
             start();
             Original<void(__thiscall*)(void*, void*)>(0x004FA3C0)(c, Next() % 3 ? ArenaPtr(kSkaterObj) : nullptr);
             return finish(0);
         },
         [=](uint8_t* c) {
             start();
             self(c)->SetSkater(Next() % 3 ? reinterpret_cast<Obj::CCompositeObject*>(ArenaPtr(kSkaterObj)) : nullptr);
             return finish(0);
         }},
        {"SaveState",
         [=](uint8_t* c) {
             start();
             Original<void(__thiscall*)(void*, void*)>(0x004FA420)(c, ArenaPtr(kOut));
             return finish(0);
         },
         [=](uint8_t* c) {
             start();
             self(c)->SaveState(ArenaPtr(kOut));
             return finish(0);
         }},
        {"Construct",
         [=](uint8_t* c) {
             start();
             return finish(reinterpret_cast<uint32_t>(Original<void*(__thiscall*)(void*)>(0x004FA4B0)(c)));
         },
         [=](uint8_t* c) {
             start();
             return finish(reinterpret_cast<uint32_t>(self(c)->Construct()));
         }},
        {"Destroy",
         [=](uint8_t* c) {
             start();
             return finish(reinterpret_cast<uint32_t>(Original<void*(__thiscall*)(void*, unsigned)>(0x004FA590)(c, Next() & 1)));
         },
         [=](uint8_t* c) {
             start();
             return finish(reinterpret_cast<uint32_t>(self(c)->Destroy(Next() & 1)));
         }},
        {"InitFromStructure",
         [=](uint8_t* c) {
             start();
             Original<void(__thiscall*)(void*, void*)>(0x004FA5C0)(c, params);
             return finish(0);
         },
         [=](uint8_t* c) {
             start();
             self(c)->InitFromStructure(params);
             return finish(0);
         }},
        {"CallMemberFunction",
         [=](uint8_t* c) {
             start();
             return finish(static_cast<uint32_t>(
                 Original<int(__thiscall*)(void*, uint32_t, void*, void*)>(0x004FA680)(c, memberName, params, nullptr)));
         },
         [=](uint8_t* c) {
             start();
             return finish(static_cast<uint32_t>(self(c)->CallMemberFunction(memberName, params, nullptr)));
         }},
        voidCase("ResetMode", 0x004FA7C0, &C::ResetMode),
        {"LookDown", [=](uint8_t* c) { start(); return finish(static_cast<uint32_t>(Original<bool(__thiscall*)(void*)>(0x004F9EB0)(c))); },
         [=](uint8_t* c) { start(); return finish(static_cast<uint32_t>(self(c)->LookDown())); }},
        voidCase("NextMode", 0x004FA7D0, &C::NextMode),
        {"Create", [=](uint8_t*) { start(); return finish(reinterpret_cast<uint32_t>(Original<void*(__cdecl*)()>(0x004FA800)())); },
         [=](uint8_t*) { start(); return finish(reinterpret_cast<uint32_t>(C::Create())); }},
        cleanupCase("CleanupDestroyBody", 0x004FBD90, &K::DestroyBody),
        {"CleanupInit",
         [=](uint8_t* c) {
             start();
             Original<void(__thiscall*)(void*, void*)>(0x004FBDA0)(c, params);
             return finish(0);
         },
         [=](uint8_t* c) {
             start();
             cleanup(c)->InitFromStructure(params);
             return finish(0);
         }},
        {"CleanupDestroy",
         [=](uint8_t* c) {
             start();
             return finish(reinterpret_cast<uint32_t>(Original<void*(__thiscall*)(void*, unsigned)>(0x004FBDB0)(c, Next() & 1)));
         },
         [=](uint8_t* c) {
             start();
             return finish(reinterpret_cast<uint32_t>(cleanup(c)->Destroy(Next() & 1)));
         }},
        cleanupCase("CleanupFinalize", 0x004FBDE0, &K::Finalize),
        cleanupCase("CleanupUpdate", 0x004FBE00, &K::Update),
        {"CleanupCreate", [=](uint8_t*) { start(); return finish(reinterpret_cast<uint32_t>(Original<void*(__cdecl*)()>(0x004FBE20)())); },
         [=](uint8_t*) { start(); return finish(reinterpret_cast<uint32_t>(K::Create())); }},
    };
    const int kCases = static_cast<int>(cases.size());
    bool same = true;
    int updates = 0;
    for (int variant = 0; variant < kCases * 600 && same; ++variant) {
        g_rng = 0x27D4EB2Fu * (variant + 9);
        FillCamera();
        ready = Next() % 4 != 0;
        static const uint32_t names[] = {0x75A516D3, 0xD7867FFE, 0x12345678};
        memberName = names[Next() % 3];
        SaveStart();
        uint32_t seed = Next() | 1;
        const Case& test = cases[variant % kCases];
        auto expected = RunOnArena(seed, test.original);
        auto actual = RunOnArena(seed, test.ours);
        if (variant % kCases < 4 && Called(actual, 0x004E4360))
            ++updates;
        same = Compare(expected, actual, test.name, variant);
    }
    GameVariable<bool>(0x007CC979) = savedReady;
    GameVariable<float>(0x007CCDB4) = savedLipTurn;
    bool covered = updates > 200;
    if (!covered)
        Log("  skater camera coverage: %d full updates", updates);
    Check(same && covered, "skater camera: Update, the target, offsets, shake, life and member functions; the cleanup state");
}

} // namespace

SELFTEST(Early, TestSkaterCamera);
