// Self-tests for the skater's projectile launch and constructor (src/game/skater/skater.cpp, 0x00582800 /
// 0x00582D90): the original and ours run on stand-in skaters and targets in an arena; what they call outside is a
// recording stand-in (floats from C++ functions jumped to), and the calls, results and the whole arena are compared.

#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/obj/compositemgr.h"
#include "game/obj/game_objects.h"
#include "game/obj/object.h"
#include "game/obj/tracker.h"
#include "game/skater/skater.h"
#include "game/obj/ped_logic.h"
#include "hook/appmain_testkit.h"
#include "hook/viewer_testkit.h"

namespace {

using namespace ViewerTestKit;
using Obj::CSkater;

// Arena layout.
constexpr uint32_t kSkater = 0x0000;     // 0x324
constexpr uint32_t kCorePhys = 0x0400;
constexpr uint32_t kTargets = 0x2000;    // target objects, 0x200 apart (+0x100 their component)
constexpr uint32_t kBlocks = 0x6000;     // small blocks, 0x100 apart

uint32_t Block(int i) { return At(kBlocks + 0x100 * i); }
uint32_t Target(int i) { return At(kTargets + 0x200 * i); }

void* g_managerVtable[4];
int g_firstTarget; // -1: none
float g_targetingDistance, g_targetingAngle;

float __cdecl GetFloatStandIn(uint32_t name)
{
    Rec(0x8F00, {name});
    return name == 0x39186247 ? g_targetingDistance : (name == 0x195A7DBA ? g_targetingAngle : 0.0f);
}

// Our replacement of a game function (stood in for too: our code calls it directly); the address itself if none.
uint32_t OursOf(uint32_t address)
{
    void* ours = AppMainTests::OursAt(address);
    return ours ? Resolve(ours) : address;
}
// A stand-in's thunk written over our replacements of `addresses` as well.
// (the stand-ins here use AtOnly: which of our replacements are stood in too is chosen by AlsoOurs)
void AlsoOurs(StandIns& s, std::initializer_list<uint32_t> addresses, void* thunk)
{
    // (only the functions skater.cpp calls directly now: others' replacements may run for real)
    static const uint32_t kDirect[] = {0x00511420, 0x00518630, 0x0046BC60, 0x00511310, 0x00519650, 0x0043D520, 0x00419A20, 0x005144D0, 0x00513AB0, 0x00431780, 0x0047B2A0, 0x005178B0, 0x004202A0, 0x00420110, 0x00511390, 0x0053AEA0, 0x00532EA0, 0x00419A90, 0x00420480, 0x0051D830, 0x0051D9C0, 0x0051DA20, 0x00407A60, 0x00511570, 0x0042BBF0, 0x00524C50, 0x00525E60, 0x00419B30, 0x0043D560, 0x00522410, 0x00512030, 0x0051BF00, 0x00506BE0, 0x00510F80, 0x0043D550, 0x00543340};
    for (uint32_t address : addresses) {
        if (std::find(std::begin(kDirect), std::end(kDirect), address) == std::end(kDirect))
            continue;
        uint32_t ours = OursOf(address);
        if (ours != address)
            s.Bytes(ours, Branch(0xE9, ours, thunk));
    }
}

void Simple(StandIns& s, std::initializer_list<uint32_t> addresses, int pop, uint32_t answer = 0)
{
    uint32_t tag = *addresses.begin() & 0xFFFFF;
    void* thunk = s.AtOnly(addresses, static_cast<uint16_t>(pop), [tag, pop, answer](Regs& r) {
        g_trace.push_back(tag);
        g_trace.push_back(Rel(r.ecx));
        for (int i = 0; i < pop / 4; ++i)
            g_trace.push_back(Rel(r.args[i]));
        return answer;
    });
    AlsoOurs(s, addresses, thunk);
}

void Install(StandIns& s)
{
    InstallCommon(s);
    for (uint32_t address : {0x00478F00u, AddressOf(static_cast<float (*)(uint32_t)>(&Script::GetFloat))})
        s.Bytes(address, Branch(0xE9, address, reinterpret_cast<void*>(&GetFloatStandIn)));
    // Components: the skater's by type; a target's PedLogic is its +0x100 block when its +0xF0 word says so.
    s.AtOnly({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Rec(0xDB90, {r.ecx, r.args[0]});
        if (r.ecx - At(kTargets) < 0x2000) {
            if (r.args[0] == 0x6EFDDFB8)
                return *reinterpret_cast<uint32_t*>(r.ecx + 0xF0) ? r.ecx + 0x100 : 0u;
            return r.ecx + 0x180; // (its skeleton)
        }
        return Block(1) + (r.args[0] & 0xF0);
    });
    s.AtOnly({0x00431C60, OursOf(0x00431C60)}, 8, [](Regs& r) {
        Rec(0x1C60, {r.ecx, r.args[0]});
        auto* v = reinterpret_cast<float*>(r.args[1]);
        v[0] = 1.5f, v[1] = 4.25f, v[2] = -0.75f + (r.args[0] & 1), v[3] = 0.0f;
        return 0u;
    });
    s.AtOnly({0x0040B090, OursOf(0x0040B090)}, 8, [](Regs& r) {
        const auto* m = reinterpret_cast<const float*>(r.ecx);
        const auto* in = reinterpret_cast<const float*>(r.args[1]);
        auto* out = reinterpret_cast<float*>(r.args[0]);
        Rec(0xB090, {r.ecx});
        for (int i = 0; i < 4; ++i)
            out[i] = in[0] * m[i] + in[1] * m[4 + i] + in[2] * m[8 + i] + in[3] * m[12 + i];
        return r.args[0];
    });
    // (0x00411840, the vector turned by a matrix: run as it is - it calls 0x0040B090, stood in; ours calls
    // RotateVector the same way)
    s.AtOnly({0x0045F2D0, AddressOf(&Obj::CCompositeObjectManager::GetFirstComponentByType)}, 4, [](Regs& r) {
        Rec(0xF2D0, {r.ecx, r.args[0]});
        return g_firstTarget < 0 ? 0u : Target(g_firstTarget) + 0x100;
    });
    s.AtOnly({0x004ED140, AddressOf(&Obj::CPedLogicComponent::GetLogicState)}, 0, [](Regs& r) {
        Rec(0xD140, {r.ecx});
        return *reinterpret_cast<uint32_t*>(r.ecx - 0x100 + 0xF4);
    });
    s.AtOnly({0x00478F90, AddressOf(static_cast<uint32_t (*)(uint32_t)>(&Script::GetSymbolValue))}, 0, [](Regs& r) {
        Rec(0x8F90, {r.args[0]});
        return 0xB0E2u;
    });
    s.AtOnly({0x00477F90, AddressOf(static_cast<void (CStruct::*)(const char*, Script::VectorValue)>(&CStruct::AddVector))}, 20,
         [](Regs& r) {
             Rec(0x7F90, {r.ecx, Text(r.args[0]), r.args[1], r.args[2], r.args[3], r.args[4]});
             return 0u;
         });
    s.AtOnly({0x00477D10, AddressOf(static_cast<void (CStruct::*)(const char*, float)>(&CStruct::AddFloat))}, 8, [](Regs& r) {
        Rec(0x7D10, {r.ecx, Text(r.args[0]), r.args[1]});
        return 0u;
    });
    Simple(s, {0x00461500, AddressOf(&Obj::CObject::CallScript)}, 8);
    s.AtOnly({0x00477D40, AddressOf(static_cast<void (CStruct::*)(uint32_t, uint32_t)>(&CStruct::AddChecksum))}, 8, [](Regs& r) {
        Rec(0x7D40, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    // The constructor's.
    s.AtOnly({0x0045E510, AddressOf(&Obj::CCompositeObject::Construct)}, 0, [](Regs& r) {
        Rec(0xE510, {r.ecx});
        return r.ecx;
    });
    s.AtOnly({0x0057B250, AddressOf(&Obj::AddBaseComponents)}, 0, [](Regs& r) {
        Rec(0xB250, {r.args[0]});
        return 0u;
    });
    Simple(s, {0x00462A90, AddressOf(&Obj::CTracker::AddAlias)}, 8);
    Simple(s, {0x0045EE20, AddressOf(&Obj::CCompositeObject::CreateComponentFromStructure)}, 8);
    g_managerVtable[1] = s.Thunk(4, [](Regs& r) {
        Rec(0x6E61, {r.ecx, r.args[0]});
        return 0u;
    });
}

void SetTarget(int i, float x, float y, float z, uint32_t pedState, int next)
{
    uint32_t object = Target(i);
    auto* o = reinterpret_cast<Obj::CCompositeObject*>(object);
    o->m_pos = {x, y, z, 1.0f};
    for (int k = 0; k < 16; ++k)
        (&o->m_matrix.row[0].x)[k] = k % 5 == 0 ? 1.0f : 0.0f;
    *reinterpret_cast<uint32_t*>(object + 0xF0) = pedState != 0;
    *reinterpret_cast<uint32_t*>(object + 0xF4) = pedState;
    auto* component = reinterpret_cast<Obj::CBaseComponent*>(object + 0x100);
    component->m_object = i == 5 ? nullptr : o; // (5: a component without an object)
    component->mp_nextOfType = next >= 0 ? reinterpret_cast<Obj::CBaseComponent*>(Target(next) + 0x100) : nullptr;
}

// The launch: from each wrist (walking or not, stance flipped or not); targets behind, too far, outside the angle, a
// shot ped, a ped (aimed at its chosen bone), a nearer object, a component without an object; the best one within
// 100 (no target given) and beyond; no targets at all. The constructor for a local skater (skater 0), a remote one
// (skater 1) and skater 2.
void TestSkaterLaunch()
{
    SaveGlobals({{0x006F0FB8, 4, {}}, {0x006F0FC0, 4, {}}});
    StandIns* s = new StandIns;
    Install(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        SetUpArena(run);
        std::vector<uint32_t>& t = traces[run];
        g_printfArgs = 3;
        GameVariable<uint32_t>(0x006F0FB8) = Block(2);
        GameVariable<uint32_t>(0x006F0FC0) = Block(3);
        auto* skater = Ptr<CSkater>(kSkater);
        skater->m_pos = {10.0f, 2.0f, -5.0f, 1.0f};
        for (int k = 0; k < 16; ++k)
            (&skater->m_matrix.row[0].x)[k] = static_cast<float>(std::cos(k * 0.7)) * (k % 5 == 0 ? 1.0f : 0.4f);
        skater->m_matrix.row[2] = {0.6f, 0.3f, 0.8f, 0.0f};
        skater->m_state284 = Ptr<uint8_t>(kBlocks + 0x100 * 4);
        *Ptr<uint32_t>(kBlocks + 0x100 * 4 + 0x2C) = Block(5);
        skater->m_corePhysics = Ptr<uint8_t>(kCorePhys);
        *Ptr<uint32_t>(kCorePhys + 0x1528) = Block(6);
        auto launch = [&]() {
            if (original)
                Original<void(__thiscall*)(void*)>(0x00582800)(skater);
            else
                skater->LaunchProjectile();
        };
        // Targets: 0 behind, 1 too far, 2 outside the angle, 3 a shot ped, 4 a ped, 5 no object, 6 an object.
        SetTarget(0, 0.0f, 2.0f, -20.0f, 0, 1);
        SetTarget(1, 400.0f, 2.0f, 500.0f, 0, 2);
        SetTarget(2, 300.0f, 2.0f, -60.0f, 0, 3);
        SetTarget(3, 80.0f, 2.0f, 110.0f, 0x54F87744, 4);
        SetTarget(4, 90.0f, 5.0f, 120.0f, 0x00A11CE5, 5);
        SetTarget(5, 0.0f, 0.0f, 0.0f, 0, 6);
        SetTarget(6, 70.0f, 3.0f, 90.0f, 0, -1);
        g_targetingDistance = 300.0f;
        g_targetingAngle = 30.0f;
        for (int walking = 0; walking < 2; ++walking) {
            *Ptr<uint8_t>(kBlocks + 0x100 * 5 + 0x118) = 0;
            *Ptr<int>(kBlocks + 0x100 * 5 + 0x1C) = walking;
            for (int flipped = 0; flipped < 2; ++flipped) {
                *Ptr<uint8_t>(kBlocks + 0x100 * 6 + 0x28) = static_cast<uint8_t>(flipped);
                g_firstTarget = 0;
                launch();
            }
        }
        *Ptr<uint8_t>(kBlocks + 0x100 * 5 + 0x118) = 1;
        g_firstTarget = 4; // (the ped first: then the nearer object)
        launch();
        SetTarget(6, 110.0f, 3.0f, 155.0f, 0, -1); // (now the farther one, after the ped: the ped stays the best)
        launch();
        SetTarget(6, 70.0f, 3.0f, 90.0f, 0, -1);
        SetTarget(6, 15.0f, 3.0f, 20.0f, 0, -1); // within 100: no target given
        g_firstTarget = 6;
        launch();
        g_firstTarget = -1;
        launch();
        skater->m_matrix.row[2] = {0.0f, 1.0f, 0.0f, 0.0f}; // (no flat heading)
        g_firstTarget = 0;
        launch();
        // The constructor.
        struct Case {
            int viewport;
            bool local;
            uint32_t id;
            int number;
        };
        *Ptr<void**>(kBlocks + 0x100 * 7) = g_managerVtable;
        for (const Case& c : {Case{0, true, 0x51, 0}, Case{1, false, 0x52, 1}, Case{1, true, 0x53, 2}}) {
            auto* fresh = Ptr<CSkater>(0x1000);
            std::memset(fresh, 0xCD, sizeof(CSkater));
            t.push_back(Rel(reinterpret_cast<uint32_t>(
                original ? Original<void*(__thiscall*)(void*, int, void*, bool, uint32_t, int, int)>(0x00582D90)(
                               fresh, c.viewport, Ptr<void>(kBlocks + 0x100 * 7), c.local, c.id, c.number, 9)
                         : fresh->Construct(c.viewport, Ptr<Obj::CCompositeObjectManager>(kBlocks + 0x100 * 7), c.local, c.id,
                                            c.number, 9))));
            *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(fresh) + 0x164) = 0; // (the name's capacity: our
                                                                                      // String clears it, the original's
                                                                                      // leaves it)
            const auto* words = reinterpret_cast<const uint32_t*>(fresh);
            for (size_t i = 0; i < sizeof(CSkater) / 4; ++i)
                t.push_back(Rel(words[i]));
            fresh->m_displayName.~String();
        }
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "skater launch");
}

} // namespace

SELFTEST(Early, TestSkaterLaunch, ViewerTestKit::Cleanup);
