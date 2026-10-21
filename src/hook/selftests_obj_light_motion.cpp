// Self-tests for the model light update component (src/game/obj/model_light_update.cpp) and the motion component's
// path members (src/game/obj/motion_path.cpp): the original and ours on the same arena, every call into the rest of
// the game stood in for and recorded (the feelers, the model component, its lights, the script structures).

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <limits>
#include <vector>

#include "game/nx/feeler.h"
#include "game/obj/model_component.h"
#include "game/obj/model_light_update.h"
#include "game/obj/motion_path.h"
#include "game/obj/smtptr.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "hook/skater3_testkit.h"

namespace {

using namespace Sk3Tests;
using Obj::CModelLightUpdateComponent;
using Obj::CMotionPathMembers;

constexpr uint32_t kComponent = 0x0000, kObject = 0x0100, kModelComponent = 0x0300, kModel = 0x0600,
                   kLights = 0x0700, kHitObject = 0x0780, kParams = 0x3000, kScript = 0x3100;

struct Answers {
    uint32_t calloc;
    uint32_t hits;      // bit n: the n-th collision query hits
    int query;          // the queries so far
    bool hitObject;     // a hit leaves an object in the feeler
};
Answers g_a;
void* g_componentVtable[16];

double __fastcall StandInHitValue(Nx::CFeeler* feeler, void*)
{
    Record({0x74A0, Rel(reinterpret_cast<uint32_t>(feeler)) == 0x57AC0000u ? 0x57AC0000u : 0u,
            Bits(feeler->m_point.y)});
    return static_cast<double>(feeler->m_point.x) / 7.0 + 0.125;
}

void* RecBools(StandIns& s, std::initializer_list<uint32_t> addresses, int args, uint32_t tag, uint32_t bools,
               std::function<uint32_t(Regs&)> answer = nullptr)
{
    return s.At(addresses, static_cast<uint16_t>(4 * args), [args, tag, bools, answer](Regs& r) {
        g_trace.push_back(tag);
        g_trace.push_back(Rel(r.ecx));
        for (int i = 0; i < args; ++i)
            g_trace.push_back(bools >> i & 1 ? r.args[i] & 0xFF : Rel(r.args[i]));
        return answer ? answer(r) : 0u;
    });
}

void StandIn(StandIns& s)
{
    StandInComponentBase(s, &g_a.calloc);
    StandInStructGetters(s);
    Rec(s, {0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 1, 0xDB90,
        [](Regs& r) { return r.args[0] == Obj::CModelComponent::kType ? Addr(kModelComponent) : 0u; });
    g_componentVtable[7] = s.Thunk(4, [](Regs& r) { // Suspend
        g_trace.insert(g_trace.end(), {0x0007, Rel(r.ecx), r.args[0] & 0xFF});
        return 0u;
    });
    // The feelers.
    s.At({0x00417760, AddressOf(static_cast<Nx::CFeeler* (Nx::CFeeler::*)()>(&Nx::CFeeler::Construct))}, 0,
         [](Regs& r) {
             Record({0x7760});
             auto* feeler = reinterpret_cast<Nx::CFeeler*>(r.ecx);
             std::memset(feeler, 0, sizeof(Nx::CFeeler));
             return r.ecx;
         });
    Rec(s, {0x004170F0, AddressOf(&Nx::CFeeler::SetIgnore)}, 2, 0x70F0);
    RecBools(s, {0x004172F0, AddressOf(&Nx::CFeeler::GetCollision)}, 2, 0x72F0, 3, [](Regs& r) {
        auto* feeler = reinterpret_cast<Nx::CFeeler*>(r.ecx);
        const auto* line = reinterpret_cast<const uint32_t*>(&feeler->m_line);
        g_trace.insert(g_trace.end(), line, line + 8);
        bool hit = (g_a.hits >> g_a.query++) & 1;
        if (hit) {
            feeler->m_point = {feeler->m_line.start.x + 0.5f, feeler->m_line.start.y - 37.25f,
                               feeler->m_line.start.z, 1.0f};
            if (g_a.hitObject)
                *reinterpret_cast<uint32_t*>(&feeler->m_object) = Addr(kHitObject);
        }
        return hit ? 1u : 0u;
    });
    s.Bytes(0x004174A0, Branch(0xE9, 0x004174A0, reinterpret_cast<void*>(&StandInHitValue)));
    uint32_t oursHit = AddressOf(&Nx::CFeeler::GetHitVertexValue);
    s.Bytes(oursHit, Branch(0xE9, oursHit, reinterpret_cast<void*>(&StandInHitValue)));
    Rec(s, {0x004637E0, AddressOf(&Obj::CRefCounted::RemoveSmartPointer)}, 1, 0x37E0);
    // The model component and its lights.
    RecBools(s, {0x00420670, AddressOf(&Obj::CModelComponent::ApplyBrightness)}, 3, 0x0670, 2);
    Rec(s, {0x0048E830}, 2, 0xE830, [](Regs&) { return 1u; });
    // The script's waits and the member-function tables.
    Rec(s, {0x004739A0, AddressOf(&Script::CScript::SetWaitOnComponent)}, 3, 0x39A0);
    Rec(s, {0x0046F060, AddressOf(&Script::RegisterMemberFunctionTable)}, 3, 0xF060, nullptr, false, true);
}

constexpr uint32_t kLightAddresses[] = {0x00422DC0, 0x00422D00, 0x00422D10, 0x00422D40,
                                        0x00422DA0, 0x00422E40, 0x004230B0};

void TestModelLightUpdate()
{
    StandIns& s = *new StandIns();
    StandIn(s);
    for (uint32_t seed = 1; seed <= 64; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            Lcg random(0x116D0000 + seed);
            g_a = {};
            g_a.hitObject = random.Below(3) == 0;
            FillArena(seed);
            g_structs.clear();
            std::vector<OriginalCode*> guards;
            if (original) {
                for (uint32_t address : kLightAddresses)
                    guards.push_back(new OriginalCode(address));
            }
            std::vector<uint32_t> trace;
            TestLifecycle(original, 0x00422DC0, 0x00422D00, 0x00422D10, kComponent, sizeof(CModelLightUpdateComponent),
                          g_a.calloc, trace);
            reinterpret_cast<void*(__thiscall*)(void*, unsigned)>(Code(original, 0x00422D10))(At(kComponent), 2);
            // Its parameters (an interval of 0 isn't given: a division by zero in both).
            auto* c = reinterpret_cast<CModelLightUpdateComponent*>(At(kComponent));
            c->m_vtable = g_componentVtable;
            c->m_object = reinterpret_cast<Obj::CCompositeObject*>(At(kObject));
            c->m_lightFlags = random.Next();
            c->m_interval = random.Next();
            c->m_counter = random.Next();
            if (random.Below(2))
                SetComponent(At(kParams), 0xE7375C4C, 0);
            if (random.Below(3))
                SetComponent(At(kParams), 0x470B8AE9, 1 + random.Below(7));
            reinterpret_cast<void(__thiscall*)(void*, void*)>(Code(original, 0x00422D40))(c, At(kParams));
            trace.insert(trace.end(), {c->m_lightFlags, c->m_interval, c->m_counter});
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x00422DA0))(c);
            reinterpret_cast<Obj::CModelComponent*>(At(kModelComponent))->mp_model =
                reinterpret_cast<Nx::CModel*>(At(kModel));
            F<uint32_t>(kModel + 0x2C) = Addr(kLights);
            // Updates (the first samples), with every combination of hits.
            if (random.Below(4) == 0)
                c->m_lightFlags &= ~Obj::MODEL_LIGHT_SAMPLED;
            for (int k = 0; k < 12; ++k) {
                g_a.hits = random.Next();
                g_a.query = 0;
                reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x004230B0))(c);
                trace.insert(trace.end(), {c->m_lightFlags, c->m_counter, Bits(c->m_brightness)});
            }
            for (int k = 0; k < 4; ++k) {
                g_a.hits = static_cast<uint32_t>(k);
                g_a.query = 0;
                reinterpret_cast<void(__thiscall*)(void*, bool, float)>(Code(original, 0x00422E40))(
                    c, (k & 1) != 0, 0.25f * static_cast<float>(seed));
            }
            for (OriginalCode* guard : guards)
                delete guard;
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            for (uint32_t i = 0; i < 0x0800; i += 4)
                trace.push_back(Rel(F<uint32_t>(i)));
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "model light update: made, destroyed, parameters, samples, updates");
    }
    delete &s;
}

constexpr uint32_t kMotionAddresses[] = {0x00423130, 0x00423150, 0x00423520, 0x00423510, 0x00423530, 0x00423160,
                                         0x00423170, 0x00423180, 0x00423190, 0x004231A0, 0x00423210, 0x00423280,
                                         0x004232F0, 0x00423360, 0x004233A0, 0x004233E0, 0x00423480, 0x00423490,
                                         0x004234B0, 0x004234D0, 0x004234F0};

void TestMotionPathMembers()
{
    StandIns& s = *new StandIns();
    StandIn(s);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (uint32_t seed = 1; seed <= 64; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            Lcg random(0x307A0000 + seed);
            FillArena(seed);
            g_structs.clear();
            std::vector<OriginalCode*> guards;
            if (original) {
                for (uint32_t address : kMotionAddresses)
                    guards.push_back(new OriginalCode(address));
            }
            std::vector<uint32_t> trace;
            auto* m = reinterpret_cast<CMotionPathMembers*>(At(kComponent));
            auto value = [&](int kind) {
                switch (kind) {
                case 0: return 0.0f;
                case 1: return nan;
                default: return static_cast<float>(random.Below(4000)) / 8.0f;
                }
            };
            auto pick = [&]() { return static_cast<int>(random.Below(9)); };
            reinterpret_cast<void(__cdecl*)()>(Code(original, 0x00423130))();
            // The flags and the speeds' updates.
            for (int k = 0; k < 24; ++k) {
                m->m_motionFlags = random.Next();
                m->m_pathFlags = random.Next();
                m->m_acceleration = value(pick());
                m->m_deceleration = value(pick());
                m->m_pathVelocity = value(pick());
                m->m_velocity = random.Below(5) == 0 ? m->m_pathVelocity : value(pick());
                m->m_stopDistance = value(pick());
                m->m_stopCovered = random.Below(5) == 0 ? m->m_stopDistance : value(pick());
                m->m_stopStartVelocity = value(pick());
                m->m_minStopVelocity = value(pick());
                m->m_frameTime = static_cast<float>(random.Below(100)) / 60.0f;
                trace.push_back(reinterpret_cast<uint32_t(__thiscall*)(void*)>(Code(original, 0x00423150))(m) & 0xFF);
                trace.push_back(reinterpret_cast<uint32_t(__thiscall*)(void*)>(Code(original, 0x00423520))(m) & 0xFF);
                trace.push_back(Rel(reinterpret_cast<uint32_t(__thiscall*)(void*)>(Code(original, 0x00423510))(m)));
                for (int step = 0; step < 3; ++step) {
                    reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x00423530))(m);
                    float v = m->m_velocity, c = m->m_stopCovered;
                    trace.push_back(v != v ? 0x7FC00000u : Bits(v));
                    trace.push_back(c != c ? 0x7FC00000u : Bits(c));
                }
            }
            // The members, each with random parameters.
            for (uint32_t address : {0x00423160u, 0x00423170u, 0x00423180u, 0x00423190u, 0x004231A0u, 0x00423210u,
                                     0x00423280u, 0x004232F0u, 0x00423360u, 0x004233A0u, 0x004233E0u, 0x00423480u,
                                     0x00423490u, 0x004234B0u, 0x004234D0u, 0x004234F0u}) {
                for (int k = 0; k < 3; ++k) {
                    g_structs.clear();
                    void* params = At(kParams);
                    if (random.Below(4))
                        SetComponent(params, 0, Bits(random.Below(5) == 0 ? 0.0f : value(2)));
                    for (uint32_t flag : {0xAAD7C80Fu, 0xA18B8F32u, 0xF414A5EAu, 0x7644323Bu, 0xD443A2BCu, 0xF915B4F3u})
                        if (random.Below(3) == 0)
                            SetComponent(params, flag, 0);
                    m->m_motionFlags = random.Next();
                    m->m_pathFlags = random.Next();
                    m->m_minStopVelocity = random.Below(3) == 0 ? 0.0f : value(2);
                    trace.push_back(reinterpret_cast<uint32_t(__thiscall*)(void*, void*, void*)>(Code(original, address))(
                        m, params, At(kScript)));
                    for (uint32_t i = 0x18; i < 0x38; i += 4)
                        trace.push_back(Rel(F<uint32_t>(kComponent + i)));
                    for (uint32_t i = 0x1A4; i < 0x1B0; i += 4)
                        trace.push_back(Rel(F<uint32_t>(kComponent + i)));
                }
            }
            for (OriginalCode* guard : guards)
                delete guard;
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "motion: the path members (flags, speeds, stops, waits) and the speed's update");
    }
    delete &s;
}

} // namespace

SELFTEST(Early, TestModelLightUpdate, RemoveStandIns);
SELFTEST(Early, TestMotionPathMembers, RemoveStandIns);
