// Self-tests for the collide-and-die component (src/game/obj/collide_and_die.cpp, 0x0041C1F0-0x0041C707), the dynamic
// light component and the light's radius (dynamic_light.cpp, 0x0041CB90-0x0041D161), and three helpers of the range:
// the game clock's time (game_clock.cpp 0x0041C200), the feeler's destructor (feeler.cpp 0x0041C350) and a smart
// pointer's assignment from a pointer (smtptr.cpp 0x0041D1D0). The original and ours, every call they make stood in
// for and recorded.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/vector.h"
#include "game/net/gamenet_manager.h"
#include "game/nx/dynamic_light.h"
#include "game/nx/feeler.h"
#include "game/obj/collide_and_die.h"
#include "game/obj/dynamic_light.h"
#include "game/obj/object.h"
#include "game/obj/smtptr.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sys/game_clock.h"
#include "hook/appmain_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace AppMainTests;
using Obj::CCollideAndDieComponent;
using Obj::CDynamicLightComponent;

struct World {
    alignas(16) uint8_t component[0x60];
    alignas(16) uint8_t object[0x120];
    alignas(16) uint8_t suspend[0x40];
    alignas(16) uint8_t projectile[0x20];
    alignas(16) uint8_t hitObject[0x20];
    alignas(16) uint8_t structMemory[0x10];
    alignas(16) uint8_t netManager[0x20];
    alignas(16) uint8_t waitAndDie[0x30];
    alignas(16) uint8_t light[0x40];
    alignas(16) uint8_t arrays[3][0x10];
    alignas(16) uint8_t pointer[0x10];
    void* objectVtable[12];
};
World g_world;

uint32_t Rel(uint32_t word)
{
    auto base = reinterpret_cast<uint32_t>(&g_world);
    if (word >= base && word < base + sizeof(World))
        return 0x77000000 + (word - base);
    return OnStack(word) ? 0x57AC0000u : word;
}

uint32_t Bits(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}

struct Answers {
    uint32_t seed;
    bool callocFails;
    bool inNetGame;
    int elapsed;
    bool hit;
    bool hitObject;
    bool haveProjectile;
    bool haveParams;
    uint32_t paramsMissing; // a name GetFloat / GetArray say they haven't
};
Answers g_a;

void FillFloats(Lcg& random, uint8_t* p, size_t size, int spread)
{
    for (size_t i = 0; i < size; i += 4) {
        float f = static_cast<float>(static_cast<int>(random.Below(2 * spread + 1)) - spread) / 7.0f;
        std::memcpy(p + i, &f, 4);
    }
}

void StandIn(StandIns& s)
{
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Record({0x1CC0, r.args[0]});
        if (g_a.callocFails)
            return 0u;
        std::memset(g_world.component, 0xCD, sizeof(g_world.component)); // (dirty: what's left unset shows)
        return reinterpret_cast<uint32_t>(g_world.component);
    });
    s.At({0x0045D730, AddressOf(&Obj::CBaseComponent::Construct)}, 0, [](Regs& r) {
        Record({0xD730, Rel(r.ecx)});
        reinterpret_cast<uint32_t*>(r.ecx)[0] = 0xBA5E0000;
        reinterpret_cast<uint32_t*>(r.ecx)[2] = 0;
        return r.ecx;
    });
    s.At({0x0045D740, AddressOf(&Obj::CBaseComponent::DestroyBody)}, 0, [](Regs& r) {
        Record({0xD740, Rel(r.ecx), Rel(*reinterpret_cast<uint32_t*>(r.ecx))});
        return 0u;
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xABA0, Rel(r.ecx), Rel(r.args[0])});
        return 0u;
    });
    s.At({0x0044ECE0, AddressOf(Tmr::GameClockElapsed)}, 0, [](Regs&) {
        Record({0xECE0});
        return static_cast<uint32_t>(g_a.elapsed);
    });
    s.At({0x0044E4F0, AddressOf(&GameNet::Manager::InNetGame)}, 0, [](Regs& r) {
        Record({0xE4F0, Rel(r.ecx)});
        return g_a.inNetGame ? 1u : 0u;
    });
    using GetFloatFn = bool (Script::CStruct::*)(uint32_t, float*, int);
    s.At({0x004766D0, AddressOf(static_cast<GetFloatFn>(&Script::CStruct::GetFloat))}, 12, [](Regs& r) {
        auto* out = reinterpret_cast<float*>(r.args[1]);
        Record({0x66D0, r.ecx, r.args[0], Rel(r.args[1]), Bits(*out), r.args[2]});
        if (!g_a.haveParams || r.args[0] == g_a.paramsMissing)
            return 0u;
        *out = static_cast<float>(r.args[0] % 1009) / 9.0f - 30.0f;
        return 1u;
    });
    using GetChecksumFn = bool (Script::CStruct::*)(uint32_t, uint32_t*, int);
    s.At({0x00476950, AddressOf(static_cast<GetChecksumFn>(&Script::CStruct::GetChecksum))}, 12, [](Regs& r) {
        auto* out = reinterpret_cast<uint32_t*>(r.args[1]);
        Record({0x6950, r.ecx, r.args[0], Rel(r.args[1]), *out, r.args[2]});
        if (!g_a.haveParams)
            return 0u;
        *out = r.args[0] ^ 0x11110000 ^ g_a.seed;
        return 1u;
    });
    using GetVectorFn = bool (Script::CStruct::*)(uint32_t, Script::VectorValue*, int);
    s.At({0x00476FB0, AddressOf(static_cast<GetVectorFn>(&Script::CStruct::GetVector))}, 12, [](Regs& r) {
        auto* out = reinterpret_cast<float*>(r.args[1]);
        Record({0x6FB0, r.ecx, r.args[0], Rel(r.args[1]), Bits(out[0]), Bits(out[1]), Bits(out[2]), Bits(out[3]), r.args[2]});
        if (!g_a.haveParams)
            return 0u;
        out[0] = 3.5f;
        out[1] = -1.25f * static_cast<float>(g_a.seed);
        out[2] = 0.75f;
        out[3] = 1.0f;
        return 1u;
    });
    using GetArrayFn = bool (Script::CStruct::*)(uint32_t, Script::CArray**, int);
    s.At({0x00476860, AddressOf(static_cast<GetArrayFn>(&Script::CStruct::GetArray))}, 12, [](Regs& r) {
        auto* out = reinterpret_cast<uint32_t*>(r.args[1]);
        Record({0x6860, r.ecx, r.args[0], Rel(r.args[1]), Rel(*out), r.args[2]});
        if (!g_a.haveParams || r.args[0] == g_a.paramsMissing)
            return 0u;
        *out = reinterpret_cast<uint32_t>(g_world.arrays[r.args[0] % 3]);
        return 1u;
    });
    s.At({0x0046CC30, AddressOf(&Script::CArray::GetElement)}, 4, [](Regs& r) {
        Record({0xCC30, Rel(r.ecx), r.args[0]});
        return (Rel(r.ecx) * 7 + r.args[0] * 0x35 + g_a.seed) & 0x1FF;
    });
    s.At({0x004023F0, AddressOf(&Mth::Vector::Normalize)}, 0, [](Regs& r) {
        auto* v = reinterpret_cast<float*>(r.ecx);
        Record({0x23F0, Rel(r.ecx), Bits(v[0]), Bits(v[1]), Bits(v[2]), Bits(v[3])});
        for (int i = 0; i < 4; ++i)
            v[i] = v[i] * 0.125f + static_cast<float>(i) * 0.0625f;
        return r.ecx;
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Record({0xDB90, Rel(r.ecx), r.args[0]});
        switch (r.args[0]) {
        case 0xCE0CA665:
            return reinterpret_cast<uint32_t>(g_world.suspend);
        case 0x7767D6D7:
            return g_a.haveProjectile ? reinterpret_cast<uint32_t>(g_world.projectile) : 0u;
        case 0x03AC8065:
            return reinterpret_cast<uint32_t>(g_world.waitAndDie);
        default:
            return 0u;
        }
    });
    g_world.objectVtable[6] = s.Thunk(0, [](Regs& r) {
        Record({0x0B06, Rel(r.ecx)});
        return 0u;
    });
    // The feeler.
    s.At({0x00417760, AddressOf(static_cast<Nx::CFeeler* (Nx::CFeeler::*)()>(&Nx::CFeeler::Construct))}, 0, [](Regs& r) {
        Record({0x7760, Rel(r.ecx)});
        auto* feeler = reinterpret_cast<Nx::CFeeler*>(r.ecx);
        feeler->m_object.mp_object = nullptr;
        feeler->m_object.mp_prev = nullptr;
        feeler->m_object.mp_next = nullptr;
        feeler->m_point = {1.5f, 2.5f, -3.5f, 1.0f};
        return r.ecx;
    });
    s.At({0x00417190, AddressOf(&Nx::CFeeler::SetStart)}, 4, [](Regs& r) {
        const auto* v = reinterpret_cast<const uint32_t*>(r.args[0]);
        Record({0x7190, Rel(r.ecx), Rel(r.args[0]), v[0], v[1], v[2], v[3]});
        return 0u;
    });
    s.At({0x004171B0, AddressOf(&Nx::CFeeler::SetEnd)}, 4, [](Regs& r) {
        const auto* v = reinterpret_cast<const uint32_t*>(r.args[0]);
        Record({0x71B0, Rel(r.ecx), Rel(r.args[0]), v[0], v[1], v[2], v[3]});
        return 0u;
    });
    s.At({0x004172F0, AddressOf(&Nx::CFeeler::GetCollision)}, 8, [](Regs& r) {
        Record({0x72F0, Rel(r.ecx), r.args[0] & 0xFF, r.args[1] & 0xFF});
        auto* feeler = reinterpret_cast<Nx::CFeeler*>(r.ecx);
        if (g_a.hitObject)
            feeler->m_object.mp_object = reinterpret_cast<Obj::CCompositeObject*>(g_world.hitObject);
        feeler->m_point = {7.25f, static_cast<float>(g_a.seed), -2.0f, 1.0f};
        return g_a.hit ? 1u : 0u;
    });
    s.At({0x004637E0, AddressOf(&Obj::CRefCounted::RemoveSmartPointer)}, 4, [](Regs& r) {
        Record({0x37E0, Rel(r.ecx), Rel(r.args[0])});
        return 0u;
    });
    s.At({0x004637C0, AddressOf(&Obj::CRefCounted::AddSmartPointer)}, 4, [](Regs& r) {
        Record({0x37C0, Rel(r.ecx), Rel(r.args[0])});
        return 0u;
    });
    s.At({0x004F2F40}, 0, [](Regs& r) {
        Record({0x2F40, Rel(r.ecx)});
        return 0u;
    });
    // The death script's parameters.
    s.At({0x00409030, AddressOf(Script::NewStruct)}, 0, [](Regs&) {
        Record({0x9030});
        return reinterpret_cast<uint32_t>(g_world.structMemory);
    });
    s.At({0x00476410, AddressOf(&Script::CStruct::Init)}, 0, [](Regs& r) {
        Record({0x6410, Rel(r.ecx)});
        return r.ecx;
    });
    using AddVectorFn = void (Script::CStruct::*)(uint32_t, Script::VectorValue);
    s.At({0x00477F70, AddressOf(static_cast<AddVectorFn>(&Script::CStruct::AddVector))}, 20, [](Regs& r) {
        Record({0x7F70, Rel(r.ecx), r.args[0], r.args[1], r.args[2], r.args[3], r.args[4]});
        return 0u;
    });
    using AddFloatFn = void (Script::CStruct::*)(uint32_t, float);
    s.At({0x00477C60, AddressOf(static_cast<AddFloatFn>(&Script::CStruct::AddFloat))}, 8, [](Regs& r) {
        Record({0x7C60, Rel(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00475790, AddressOf(static_cast<void (*)(uint32_t, Script::CStruct*, Obj::CObject*, bool, const char*)>(
                          &Script::RunScript))},
         0, [](Regs& r) {
             Record({0x5790, r.args[0], Rel(r.args[1]), Rel(r.args[2]), r.args[3] & 0xFF, r.args[4]});
             return 0u;
         });
    s.At({0x00477090, AddressOf(&Script::CStruct::Clear)}, 0, [](Regs& r) {
        Record({0x7090, Rel(r.ecx)});
        return 0u;
    });
    s.At({0x00409070, AddressOf(Script::DeleteStruct)}, 0, [](Regs& r) {
        Record({0x9070, Rel(r.args[0])});
        return 0u;
    });
    // The dynamic light.
    s.At({0x0048FB80}, 0, [](Regs& r) {
        Record({0xFB80, r.args[0]});
        return reinterpret_cast<uint32_t>(g_world.light);
    });
}

void ResetWorld(uint32_t seed)
{
    Lcg random(0xC0DE0000 + seed);
    FillFloats(random, g_world.component, sizeof(g_world.component), 2000);
    FillFloats(random, g_world.object, sizeof(g_world.object), 2000);
    FillFloats(random, g_world.light, sizeof(g_world.light), 2000);
    auto* object = reinterpret_cast<Obj::CCompositeObject*>(g_world.object);
    object->m_vtable = g_world.objectVtable;
    object->m_id = 0x0B1E0000 + seed;
    auto* c = reinterpret_cast<Obj::CBaseComponent*>(g_world.component);
    c->m_object = object;
    std::memset(g_world.suspend, 0, sizeof(g_world.suspend));
    std::memset(g_world.structMemory, 0, sizeof(g_world.structMemory));
}

constexpr uint32_t kCollideAddresses[] = {0x0041C370, 0x0041C210, 0x0041C1F0, 0x0041C280, 0x0041C2B0,
                                          0x0041C330, 0x0041C3D0, 0x0041C200, 0x0041C350, 0x0041D1D0};

// Made (and out of memory), destroyed; InitFromStructure with and without parameters; Finalize; Update: suspended
// (outside a network game / inside one), past its lifetime (and exactly at it), dying (before / after its death time),
// the first update and later ones, a hit or none (with and without a projectile collision component, a death script,
// an object hit).
void TestCollideAndDieComponent()
{
    StandIns& s = *new StandIns();
    StandIn(s);
    GameMemory memory({{0x00696D60, 4}, {0x006FA7D0, 4}});
    for (uint32_t seed = 1; seed <= 48; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            ResetWorld(seed);
            g_a = {};
            g_a.seed = seed;
            GameVariable<int>(0x00696D60) = static_cast<int>(seed * 997u) - 20000;
            GameVariable<void*>(0x006FA7D0) = g_world.netManager;
            std::vector<OriginalCode*> guards;
            if (original) {
                for (uint32_t address : kCollideAddresses)
                    guards.push_back(new OriginalCode(address));
            }
            std::vector<uint32_t> trace;
            auto* c = reinterpret_cast<CCollideAndDieComponent*>(g_world.component);
            auto* params = reinterpret_cast<Script::CStruct*>(0x5C700000 + seed);
            g_a.elapsed = static_cast<int>(seed * 31u);
            g_a.callocFails = seed == 3;
            trace.push_back(Rel(reinterpret_cast<uint32_t(__cdecl*)()>(Code(original, 0x0041C370))()));
            g_a.callocFails = false;
            for (size_t i = 0; i < sizeof(CCollideAndDieComponent) / 4; ++i)
                trace.push_back(Rel(reinterpret_cast<uint32_t*>(g_world.component)[i]));
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x0041C1F0))(c);
            trace.push_back(Rel(reinterpret_cast<uint32_t(__thiscall*)(void*, unsigned)>(Code(original, 0x0041C280))(
                c, seed % 3)));
            trace.push_back(static_cast<uint32_t>(reinterpret_cast<int(__cdecl*)()>(Code(original, 0x0041C200))()));
            ResetWorld(seed);
            g_a.haveParams = seed % 4 != 0;
            reinterpret_cast<void(__thiscall*)(void*, void*)>(Code(original, 0x0041C2B0))(c, params);
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x0041C330))(c);
            for (size_t i = 0; i < sizeof(CCollideAndDieComponent) / 4; ++i)
                trace.push_back(Rel(reinterpret_cast<uint32_t*>(g_world.component)[i]));
            // Updates.
            Lcg random(0xD1E00000 + seed);
            c->m_madeTime = static_cast<uint32_t>(GameVariable<int>(0x00696D60));
            c->m_dying = (seed & 1) != 0;
            c->m_deathTime = static_cast<uint32_t>(GameVariable<int>(0x00696D60)) + 500;
            c->m_firstUpdate = (seed & 2) != 0;
            c->m_deathScript = seed % 3 == 0 ? 0u : 0xDEAD0000 + seed;
            for (int k = 0; k < 8; ++k) {
                uint32_t pick = random.Next();
                g_world.suspend[0x30] = pick % 5 == 0 ? 1 : 0;
                g_a.inNetGame = pick % 7 == 0;
                static const int elapsed[] = {0, 499, 500, 501, 9999, 10000, 10001, 20000};
                g_a.elapsed = elapsed[(pick >> 3) % 8];
                g_a.hit = (pick >> 6) % 3 != 0;
                g_a.hitObject = (pick >> 8) & 1;
                g_a.haveProjectile = (pick >> 9) & 1;
                if ((pick >> 10) % 4 == 0)
                    c->m_dying = false;
                FillFloats(random, g_world.object + 0x4C, 0x10, 2000); // (a new position)
                if (k == 2) { // (24 up from here isn't a float: rounding it before adding -0.75 would differ)
                    auto* object = reinterpret_cast<Obj::CCompositeObject*>(g_world.object);
                    object->m_pos.y = 16777215.0f;
                    object->m_vel.y = -6.5f; // (-0.75 once the stand-in normalised it)
                    c->m_radius = 1.0f;
                    c->m_dying = false;
                    g_world.suspend[0x30] = 0;
                    g_a.elapsed = 0;
                }
                reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x0041C3D0))(c);
                for (size_t i = 0; i < sizeof(CCollideAndDieComponent) / 4; ++i)
                    trace.push_back(Rel(reinterpret_cast<uint32_t*>(g_world.component)[i]));
            }
            // The feeler's destructor (an object or none) and the smart pointer's assignment (an object or none, from
            // one or none).
            for (int k = 0; k < 4; ++k) {
                alignas(16) uint8_t feeler[sizeof(Nx::CFeeler)];
                std::memset(feeler, 0x5A, sizeof(feeler));
                auto* pointer = reinterpret_cast<uint32_t*>(feeler + 0x98);
                pointer[0] = k & 1 ? reinterpret_cast<uint32_t>(g_world.hitObject) : 0u;
                reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x0041C350))(feeler);
                trace.push_back(Rel(pointer[0]));
                trace.push_back(pointer[1]);
                auto* smart = reinterpret_cast<uint32_t*>(g_world.pointer);
                smart[0] = k & 1 ? reinterpret_cast<uint32_t>(g_world.projectile) : 0u;
                smart[1] = 0x1111;
                smart[2] = 0x2222;
                void* to = k & 2 ? g_world.hitObject : nullptr;
                trace.push_back(Rel(reinterpret_cast<uint32_t(__thiscall*)(void*, void*)>(Code(original, 0x0041D1D0))(
                    smart, to)));
                trace.push_back(Rel(smart[0]));
            }
            for (OriginalCode* guard : guards)
                delete guard;
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            for (size_t i = 0; i < sizeof(g_world.structMemory) / 4; ++i)
                trace.push_back(Rel(reinterpret_cast<uint32_t*>(g_world.structMemory)[i]));
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "collide-and-die component: made, initialised, its updates; the feeler's destructor, "
                                  "the smart pointer's assignment, the game clock's time");
    }
    delete &s;
}

constexpr uint32_t kLightAddresses[] = {0x0041CB90, 0x0041D0F0, 0x0041CBD0, 0x0041D0C0,
                                        0x0041CBF0, 0x0041CDA0, 0x0041CDC0, 0x0041CFC0};

// Made, destroyed (with a light and without); InitFromStructure with every parameter or only the asserted ones, some
// missing; Update from many lives (no middle, before / at / after the middle, elapsed past 2^31, NaN); the members.
void TestDynamicLightComponent()
{
    StandIns& s = *new StandIns();
    StandIn(s);
    float nan = std::numeric_limits<float>::quiet_NaN();
    for (uint32_t seed = 1; seed <= 40; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            ResetWorld(seed);
            g_a = {};
            g_a.seed = seed;
            std::vector<OriginalCode*> guards;
            if (original) {
                for (uint32_t address : kLightAddresses)
                    guards.push_back(new OriginalCode(address));
            }
            std::vector<uint32_t> trace;
            auto* c = reinterpret_cast<CDynamicLightComponent*>(g_world.component);
            auto* params = reinterpret_cast<Script::CStruct*>(0x5C700000 + seed);
            void* script = reinterpret_cast<void*>(0x5C900000 + seed);
            g_a.callocFails = seed == 5;
            trace.push_back(Rel(reinterpret_cast<uint32_t(__cdecl*)()>(Code(original, 0x0041D0F0))()));
            g_a.callocFails = false;
            for (size_t i = 0; i < sizeof(CDynamicLightComponent) / 4; ++i)
                trace.push_back(Rel(reinterpret_cast<uint32_t*>(g_world.component)[i]));
            c->mp_light = seed & 1 ? reinterpret_cast<Nx::CDynamicLight*>(g_world.light) : nullptr;
            c->mp_waitAndDie = g_world.waitAndDie;
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x0041CBD0))(c);
            for (size_t i = 0; i < sizeof(CDynamicLightComponent) / 4; ++i)
                trace.push_back(Rel(reinterpret_cast<uint32_t*>(g_world.component)[i]));
            for (size_t i = 0; i < 0x2C / 4; ++i)
                trace.push_back(Rel(reinterpret_cast<uint32_t*>(g_world.light)[i]));
            c->mp_light = nullptr;
            trace.push_back(Rel(reinterpret_cast<uint32_t(__thiscall*)(void*, unsigned)>(Code(original, 0x0041D0C0))(
                c, seed % 3)));
            ResetWorld(seed);
            c->mp_light = nullptr;
            // InitFromStructure (every parameter, or one of the optional ones missing).
            g_a.haveParams = true;
            static const uint32_t optional[] = {0, 0x856BC5E2, 0x8E39913D, 0xCEF9F4FD, 0x2A6B6FE2,
                                                0x3F243A3C, 0xE1CE9780, 0x0481113D};
            g_a.paramsMissing = optional[seed % 8];
            reinterpret_cast<void(__thiscall*)(void*, void*)>(Code(original, 0x0041CBF0))(c, params);
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x0041CDA0))(c);
            for (size_t i = 0; i < sizeof(CDynamicLightComponent) / 4; ++i)
                trace.push_back(Rel(reinterpret_cast<uint32_t*>(g_world.component)[i]));
            // Updates over the life.
            Lcg random(0x11E00000 + seed);
            static const float middles[] = {-1.0f, 0.5f, 0.25f, 0.0f, 1.0f, 0.7f};
            c->m_middleTime = seed % 13 == 12 ? nan : middles[seed % 6];
            for (int k = 0; k < 8; ++k) {
                uint32_t pick = random.Next();
                uint32_t total = 1000 + (pick % 3) * 1234;
                static const uint32_t elapsed[] = {0, 1, 500, 999, 1000, 0x80000005, 250, 617};
                uint32_t e = elapsed[(pick >> 4) % 8];
                if (k == 7)
                    total = 0x90000000u;
                *reinterpret_cast<uint32_t*>(g_world.waitAndDie + 0x1C) = e;
                *reinterpret_cast<uint32_t*>(g_world.waitAndDie + 0x20) = total;
                if (k == 3 && c->m_middleTime > 0.0f) // (exactly at the middle)
                    *reinterpret_cast<uint32_t*>(g_world.waitAndDie + 0x1C) =
                        static_cast<uint32_t>(static_cast<double>(c->m_middleTime) * total);
                reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x0041CDC0))(c);
                for (size_t i = 0; i < 0x2C / 4; ++i)
                    trace.push_back(Rel(reinterpret_cast<uint32_t*>(g_world.light)[i]));
            }
            // The members, and another; the light's radius (0, NaN).
            for (uint32_t name : {0xFAAD1C11u, 0x86E1FA56u, 0x1A0F4C50u, 0x1A0F4C51u}) {
                trace.push_back(reinterpret_cast<uint32_t(__thiscall*)(void*, uint32_t, void*, void*)>(
                    Code(original, 0x0041CFC0))(c, name, params, script));
                for (size_t i = 0; i < 0x2C / 4; ++i)
                    trace.push_back(Rel(reinterpret_cast<uint32_t*>(g_world.light)[i]));
            }
            for (float radius : {0.0f, -0.0f, 3.0f, nan}) {
                reinterpret_cast<void(__thiscall*)(void*, float)>(Code(original, 0x0041CB90))(g_world.light, radius);
                trace.push_back(reinterpret_cast<uint32_t*>(g_world.light)[6]);
                trace.push_back(reinterpret_cast<uint32_t*>(g_world.light)[7]);
            }
            for (OriginalCode* guard : guards)
                delete guard;
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "dynamic light component: made, initialised, updated over its life, the members");
    }
    delete &s;
}

} // namespace

SELFTEST(Early, TestCollideAndDieComponent, RemoveStandIns);
SELFTEST(Early, TestDynamicLightComponent, RemoveStandIns);
