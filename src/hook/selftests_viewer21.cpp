// Self-tests for the animation movies' updates (src/game/obj/anim_movies.cpp): the object movie's Update (every object
// the asset names moved to its frame) and the camera movie's PlaceCamera (the camera from the asset or at rest, aimed
// at an object or a point). The asset's frame reader, the object lookups and the camera are recording stand-ins; the
// maths helpers (quaternion to matrix, TranslateLocal, Normalize, matrix copy) run for real. Calls, results and the
// whole arena are compared.

#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "game/gfx/camera.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/anim_movies.h"
#include "game/obj/node_objects.h"
#include "game/obj/object.h"
#include "game/obj/objman.h"
#include "game/script/names.h"
#include "game/gfx/boned_anim_frame_data.h"
#include "hook/viewer_testkit.h"

namespace {

using namespace ViewerTestKit;
using Obj::CCameraAnimMovie;
using Obj::CObjectAnimMovie;

constexpr uint32_t kObjects = 0x0000; // the object movie
constexpr uint32_t kCamera = 0x0100;  // the camera movie
constexpr uint32_t kAsset = 0x0300;   // +8 the object count, +0x1C the names
constexpr uint32_t kCamPos = 0x0400;
constexpr uint32_t kCamMatrix = 0x0440;
constexpr uint32_t kSkaters = 0x0500; // what 0x007CE464 points at (+0x20: two skaters)
constexpr uint32_t kWorld = 0x2000;   // the objects (0x200 each)

void* g_timelineVtable[4];
void* g_componentVtable[2];
std::map<uint32_t, uint32_t> g_objects; // id -> object
bool g_component;

void Install(StandIns& s)
{
    InstallCommon(s);
    g_printfArgs = 1;
    g_timelineVtable[2] = s.Thunk(0, [](Regs& r) {
        Rec(0x0702, {r.ecx});
        reinterpret_cast<float*>(r.ecx)[6] += 0.25f;
        return 0u;
    });
    s.At({0x00482F40, AddressOf(&Gfx::CBonedAnimFrameData::FireEvents)}, 16, [](Regs& r) {
        Rec(0x2F40, {r.ecx, r.args[0], r.args[1], r.args[2], r.args[3] & 0xFF});
        return 0u;
    });
    s.At({0x00482F00, AddressOf(&Gfx::CBonedAnimFrameData::GetBoneName)}, 4, [](Regs& r) {
        Rec(0x2F00, {r.ecx, r.args[0]});
        return 0x0B1E0u + r.args[0];
    });
    // The asset's frame: per object a rotation, a position and a flag.
    s.At({0x004844C0, AddressOf(&Gfx::CBonedAnimFrameData::GetFloatKeyPose)}, 0x14, [](Regs& r) {
        auto local = [](uint32_t p) { return InArena(p) ? p : 0x5u; }; // (the camera's are stack locals)
        Rec(0x44C0, {r.ecx, local(r.args[0]), local(r.args[1]), local(r.args[2]), r.args[3], r.args[4]});
        int count = r.ecx == At(kAsset) ? *reinterpret_cast<int*>(r.ecx + 8) : 1;
        auto* quats = reinterpret_cast<float*>(r.args[0]);
        auto* positions = reinterpret_cast<float*>(r.args[1]);
        for (int i = 0; i < count; ++i) {
            const float q[4] = {0.1f * i, 0.5f, -0.3f, 0.8f};
            if (quats && i != 1) // (the second keeps what Update put there)
                std::memcpy(quats + 4 * i, q, 16);
            const float p[4] = {10.0f + i, -2.5f, 3.0f * i, 1.0f};
            std::memcpy(positions + 4 * i, p, 16);
            reinterpret_cast<uint8_t*>(r.args[2])[i] = static_cast<uint8_t>(i & 1);
        }
        return 0u;
    });
    s.At({0x00460AD0, AddressOf(&Obj::FindObject)}, 0, [](Regs& r) {
        Rec(0x0AD0, {r.args[0]});
        auto it = g_objects.find(r.args[0]);
        return it == g_objects.end() ? 0u : it->second;
    });
    s.At({0x004619F0, AddressOf(&Obj::CBaseManager::GetObjectByID)}, 4, [](Regs& r) {
        Rec(0x19F0, {r.ecx, r.args[0]});
        auto it = g_objects.find(r.args[0]);
        return it == g_objects.end() ? 0u : it->second;
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Rec(0xDB90, {r.ecx, r.args[0]});
        return g_component ? r.ecx + 0x180 : 0u;
    });
    g_componentVtable[1] = s.Thunk(0, [](Regs& r) {
        Rec(0x0C01, {r.ecx});
        return 0u;
    });
    s.At({0x00405FA0, AddressOf(&Mth::Vector::Print)}, 0, [](Regs& r) {
        Rec(0x5FA0, {r.ecx});
        return 0u;
    });
    s.At({0x00404BA0, AddressOf(&Mth::Matrix::Print)}, 0, [](Regs& r) {
        Rec(0x4BA0, {r.ecx});
        return 0u;
    });
    s.At({0x0046D110, AddressOf(&Script::FindChecksumNameOrUnknown)}, 0, [](Regs& r) {
        Rec(0xD110, {r.args[0]});
        return reinterpret_cast<uint32_t>("an object");
    });
    // The camera.
    s.At({0x00485A00, AddressOf(&Gfx::Camera::GetPos)}, 0, [](Regs& r) {
        Rec(0x5A00, {r.ecx});
        return At(kCamPos);
    });
    s.At({0x00485A10, AddressOf(&Gfx::Camera::GetMatrix)}, 0, [](Regs& r) {
        Rec(0x5A10, {r.ecx});
        return At(kCamMatrix);
    });
    s.At({0x00485B20, AddressOf(&Gfx::Camera::SetHFOV)}, 4, [](Regs& r) {
        Rec(0x5B20, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00468F20, AddressOf(&Obj::CMovieDetails::CheckDone)}, 4, [](Regs& r) {
        Rec(0x8F20, {r.ecx, r.args[0]});
        return 0u;
    });
}

// The camera movie's field of view global: a float returned in ST0 (a C++ function jumped to).
float __cdecl FieldOfViewGlobal(uint32_t name)
{
    Rec(0x8F01, {name});
    return 72.0f;
}

void SetObject(int index, uint32_t id)
{
    uint32_t at = kWorld + 0x200 * index;
    *Ptr<void**>(at) = nullptr;
    float* f = Ptr<float>(at);
    for (int i = 0; i < 0x40; ++i)
        f[0x4C / 4 + i] = 0.125f * ((i * 7 + index * 3) % 11) - 0.5f;
    *Ptr<void**>(at + 0x180) = g_componentVtable;
    g_objects[id] = At(at);
}

void TestAnimMovieUpdates()
{
    SaveGlobals({{0x007CE464, 4, {}}, {0x006F0FB8, 4, {}}});
    StandIns* s = new StandIns;
    Install(*s);
    for (uint32_t address : {0x00478F00u, AddressOf(static_cast<float (*)(uint32_t)>(&Script::GetFloat))})
        s->Bytes(address, Branch(0xE9, address, reinterpret_cast<void*>(&FieldOfViewGlobal)));
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        SetUpArena(run);
        g_objects.clear();
        g_integers.clear();
        std::vector<uint32_t>& t = traces[run];
        auto mark = [&](uint32_t m) { g_trace.push_back(0xEE000000 | m); };
        GameVariable<uint32_t>(0x007CE464) = At(kSkaters);
        GameVariable<uint32_t>(0x006F0FB8) = At(0x0580);
        for (int i = 0; i < 2; ++i) {
            *Ptr<uint32_t>(kSkaters + 0x20 + 4 * i) = At(0x0520 + 0x10 * i);
            *Ptr<uint32_t>(0x0520 + 0x10 * i + 0x18) = At(0x0540 + 0x20 * i);
        }
        auto* o = Ptr<CObjectAnimMovie>(kObjects);
        auto* c = Ptr<CCameraAnimMovie>(kCamera);
        *Ptr<void**>(kObjects + 0x30) = g_timelineVtable;
        *Ptr<void**>(kCamera + 0x60) = g_timelineVtable;
        o->m_timeline.m_current = 1.5f;
        c->m_timeline.m_current = 0.75f;
        o->m_base.m_camera = Ptr<Gfx::Camera>(0x0600);
        c->m_base.m_camera = Ptr<Gfx::Camera>(0x0640);
        // The object movie: 4 objects (the third missing), with and without the debug global, with and without the
        // model component.
        *Ptr<int>(kAsset + 8) = 4;
        o->m_anim = Ptr<void>(kAsset);
        for (int i : {0, 1, 3})
            SetObject(i, 0x0B1E0u + i);
        for (int pass = 0; pass < 2; ++pass) {
            g_integers[0x2F510F45] = pass;
            g_component = pass == 0;
            if (original)
                Original<void(__thiscall*)(void*)>(0x005876A0)(o);
            else
                o->Update();
            mark(1 + pass);
        }
        // The camera movie: from the asset; at rest (the field of view 0: the global's; then its own); with a skater
        // flag up.
        auto place = [&](uint32_t m) {
            for (int i = 0; i < 32; ++i)
                Ptr<float>(kCamPos)[i] = 9.0f;
            if (original)
                Original<void(__thiscall*)(void*)>(0x00587A50)(c);
            else
                c->PlaceCamera();
            for (int i = 0; i < 20; ++i) // (each placing kept: the next overwrites it)
                t.push_back(Ptr<uint32_t>(kCamPos)[i < 4 ? i : 0x10 + i - 4]);
            mark(m);
        };
        c->m_hasTarget = false;
        c->m_anim = Ptr<void>(0x0340);
        place(10);
        c->m_anim = nullptr;
        c->m_fov = 0.0f;
        *Ptr<uint8_t>(0x0540 + 0x20) = 0x40;
        place(11);
        c->m_fov = 65.0f;
        *Ptr<uint8_t>(0x0540 + 0x20 + 0x20) = 0x41;
        place(12);
        // Aimed at an object (missing; there with each offset combination), from the asset and at rest.
        c->m_hasTarget = true;
        c->m_target = 0x7A26E7;
        place(13);
        SetObject(5, 0x7A26E7);
        const float target[4] = {1.0f, 0.5f, -2.0f, 1.0f};
        const float look[4] = {0.0f, 3.0f, 4.0f, 1.0f};
        std::memcpy(c->m_targetOffset, target, 16);
        std::memcpy(c->m_lookOffset, look, 16);
        for (int combo = 0; combo < 8; ++combo) {
            c->m_targetOffsetSet = (combo & 1) != 0;
            c->m_lookOffsetSet = (combo & 2) != 0;
            c->m_anim = (combo & 4) ? Ptr<void>(0x0340) : nullptr;
            place(20 + combo);
        }
        // A fixed point: apart; on top of it (no length).
        c->m_target = 0xC588EEBC;
        c->m_anim = nullptr;
        place(30);
        std::memcpy(c->m_targetOffset, look, 16);
        place(31);
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "anim movie updates");
}

} // namespace

SELFTEST(Early, TestAnimMovieUpdates, ViewerTestKit::Cleanup);
