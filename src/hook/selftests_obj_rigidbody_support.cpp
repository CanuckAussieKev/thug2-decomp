// Self-tests for the rigid body's code before its life and physics (rigidbody_support.cpp) and the two maths
// helpers beside it (mth/rigidbody_math.cpp): original vs ours on a stand-in component in skater_core_testkit.h's
// arena, every callee stood in for; the world point arrays, the pushers and the debug switches (globals) set the
// same before each run and recorded after it.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/mth/matrix.h"
#include "game/mth/quat.h"
#include "game/mth/random.h"
#include "game/mth/vector.h"
#include "game/obj/object.h"
#include "game/obj/rigidbody_component.h"
#include "game/obj/sound_component.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/motion_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace MotionKit;
using Obj::CRigidBodyComponent;

constexpr uint32_t kComp = kMotion;
constexpr uint32_t kContacts = kMine;          // up to 24 points (0x10 apart)
constexpr uint32_t kSpheres = kMine + 0x200;   // up to 24 spheres (0x14 apart)
constexpr uint32_t kSprings = kMine + 0x400;   // up to 2 springs (0x24 apart: the world array has room for 2)
constexpr uint32_t kVtable = kMine + 0x500;    // a stand-in vtable (16 slots)
constexpr uint32_t kEntry = kMine + 0x580;     // a member function table entry
constexpr uint32_t kSound = kMine + 0x600;     // the sound component

// The globals the code reads and writes: the world arrays and the pushers (0x00694E30-0x006951B0), their reach and
// the two float / time globals after it (0x006939B0-0x006939DC).
constexpr uint32_t kWorldStart = 0x00694E30, kWorldEnd = 0x006951B0;
constexpr uint32_t kPusherStart = 0x006939B0, kPusherEnd = 0x006939DC;
std::vector<uint8_t> g_world, g_pusher;            // this variant's
std::vector<uint8_t> g_savedWorld, g_savedPusher;  // the real ones (put back at the end)

void Save()
{
    g_savedWorld.assign(reinterpret_cast<uint8_t*>(kWorldStart), reinterpret_cast<uint8_t*>(kWorldEnd));
    g_savedPusher.assign(reinterpret_cast<uint8_t*>(kPusherStart), reinterpret_cast<uint8_t*>(kPusherEnd));
}
void Restore()
{
    std::memcpy(reinterpret_cast<void*>(kWorldStart), g_savedWorld.data(), g_savedWorld.size());
    std::memcpy(reinterpret_cast<void*>(kPusherStart), g_savedPusher.data(), g_savedPusher.size());
}
// Before a run: this variant's globals; after: them recorded.
void PutGlobals()
{
    std::memcpy(reinterpret_cast<void*>(kWorldStart), g_world.data(), g_world.size());
    std::memcpy(reinterpret_cast<void*>(kPusherStart), g_pusher.data(), g_pusher.size());
}
void RecordGlobals()
{
    for (uint32_t a = kWorldStart; a < kWorldEnd; a += 4)
        Record(*reinterpret_cast<const uint32_t*>(a));
    for (uint32_t a = kPusherStart; a < kPusherEnd; a += 4)
        Record(*reinterpret_cast<const uint32_t*>(a));
}
void RandomGlobals()
{
    g_world.resize(kWorldEnd - kWorldStart);
    g_pusher.resize(kPusherEnd - kPusherStart);
    for (size_t i = 0; i < g_world.size(); i += 4) {
        float f = RichFloat(50.0f);
        std::memcpy(&g_world[i], &f, 4);
    }
    for (size_t i = 0; i < g_pusher.size(); i += 4) {
        float f = RichFloat(50.0f);
        std::memcpy(&g_pusher[i], &f, 4);
    }
}

CRigidBodyComponent* Body() { return reinterpret_cast<CRigidBodyComponent*>(ArenaPtr(kComp)); }

// Some special floats: 0, -0, exact values, a NaN now and then.
float Odd(float range)
{
    switch (Next() % 12) {
    case 0:
        return 0.0f;
    case 1:
        return -0.0f;
    case 2:
        return std::nanf("");
    case 3:
        return static_cast<float>(static_cast<int>(Next() % 7) - 3);
    default:
        return RichFloat(range);
    }
}

// A random body: the counts, arrays, matrix, position, velocities and the rest at random.
void RandomBody()
{
    ArenaAt<uint8_t*>(kComp) = ArenaPtr(kVtable);
    ArenaAt<uint8_t*>(kComp + 0x60) = ArenaPtr(kContacts);
    ArenaAt<uint16_t>(kComp + 0x8C) = static_cast<uint16_t>(Next() % 25);
    ArenaAt<uint8_t*>(kComp + 0x90) = ArenaPtr(kSpheres);
    ArenaAt<uint16_t>(kComp + 0x94) = static_cast<uint16_t>(Next() % 25);
    ArenaAt<uint8_t*>(kComp + 0x98) = ArenaPtr(kSprings);
    ArenaAt<uint16_t>(kComp + 0x9C) = static_cast<uint16_t>(Next() % 3);
    for (int i = 0; i < 2; ++i)
        ArenaAt<uint8_t>(kSprings + i * 0x24 + 0x20) = Next() % 3 != 0;
    for (uint32_t i = 0; i < 0x40; i += 4)
        ArenaAt<float>(kComp + 0x150 + i) = RichFloat(1.5f);
    for (uint32_t i = 0; i < 0x10; i += 4)
        ArenaAt<float>(kComp + 0x110 + i) = RichFloat(400.0f);
    ArenaAt<uint32_t>(kComp + 0xCC) = Next() % 2 ? Next() : 0; // the settle script
    ArenaAt<int8_t>(kComp + 0x191) = static_cast<int8_t>(static_cast<int>(Next() % 8) - 2);
    ArenaAt<uint32_t>(kComp + 0xF4) = Next() % 4;
}

template <typename Call>
std::vector<uint32_t> Run(uint32_t seed, Call call)
{
    return RunOnArena(seed, [&](uint8_t*) {
        PutGlobals();
        uint32_t result = call();
        RecordGlobals();
        return result;
    });
}
template <typename Setup, typename Original, typename Ours>
bool CompareRuns(const char* what, int variants, Setup setup, Original original, Ours ours)
{
    for (int variant = 0; variant < variants; ++variant) {
        g_rng = 0x2545F491u * static_cast<uint32_t>(variant + 5);
        g_params.clear();
        g_components.clear();
        FillMotionArena();
        RandomGlobals();
        setup(variant);
        SaveStart();
        uint32_t seed = Next();
        auto expected = Run(seed, original);
        auto actual = Run(seed, ours);
        if (!Compare(expected, actual, what, variant))
            return false;
    }
    return true;
}
// A member function of the body: original vs ours.
template <typename Setup>
bool CompareMember(const char* what, uint32_t address,
                   bool (CRigidBodyComponent::*ours)(Script::CStruct*, Script::CScript*), int variants, Setup setup)
{
    return CompareRuns(
        what, variants, setup,
        [address] {
            return static_cast<uint32_t>(
                Original<bool(__thiscall*)(void*, void*, void*)>(address)(Body(), Params(), TheScript()));
        },
        [ours] { return static_cast<uint32_t>((Body()->*ours)(Params(), TheScript())); });
}
// A plain member (no arguments): original vs ours.
bool ComparePlain(const char* what, uint32_t address, void (CRigidBodyComponent::*ours)(), int variants,
                  void (*setup)(int))
{
    return CompareRuns(
        what, variants, setup,
        [address] {
            Original<void(__thiscall*)(void*)>(address)(Body());
            return 0u;
        },
        [ours] {
            (Body()->*ours)();
            return 0u;
        });
}

void TestRigidBodyMembers()
{
    Save();
    using Fn = void (*)(void*, const uint32_t*, int);
    StandIns standIns(
        {GetFloat(), ContainsFlag(),
         {0x0046F060, 0, 3, Ret::None, false, 0, {Ours(static_cast<Fn>(&Script::RegisterMemberFunctionTable))}},
         {0x005480A0, 4, 1, Ret::None, true, 0},                   // (vtable slot 2: InitFromStructure)
         {0x0044E4F0, 12, 3, Ret::Int, true, 0},                   // (vtable slot 12: CallMemberFunction)
         {0x00433E90, 8, 2, Ret::Int, true, 0}});                  // (a member function)
    auto setup = [](int) {
        RandomBody();
        ArenaAt<uint32_t>(kVtable + 2 * 4) = 0x005480A0;
        ArenaAt<uint32_t>(kVtable + 12 * 4) = 0x0044E4F0;
        if (Next() % 3)
            Give(0, Odd(5.0f));
        if (Next() % 3 == 0)
            GiveFlag(0x019176C5);
        if (Next() % 3 == 0)
            GiveFlag(0x4A07C332);
    };
    bool ok = CompareMember("RigidBody_IgnoreSkater", 0x0042BB60, &CRigidBodyComponent::RigidBody_IgnoreSkater, 300,
                            setup);
    ok = ok && CompareMember("RigidBody_Disable", 0x0042BBB0, &CRigidBodyComponent::RigidBody_Disable, 10, setup);
    ok = ok && CompareMember("RigidBody_Reset", 0x0042BBD0, &CRigidBodyComponent::RigidBody_Reset, 10, setup);
    ok = ok && CompareRuns(
                   "RigidBody RegisterMemberFunctions", 2, [](int) {},
                   [] {
                       Original<void(__cdecl*)()>(0x0042BB00)();
                       return 0u;
                   },
                   [] {
                       CRigidBodyComponent::RegisterMemberFunctions();
                       return 0u;
                   });
    ok = ok && CompareRuns(
                   "RunMemberFunction", 40,
                   [setup](int v) {
                       setup(v);
                       auto* entry = reinterpret_cast<Script::CMemberFunctionEntry*>(ArenaPtr(kEntry));
                       entry->mName = Next();
                       entry->mCode = v % 2 ? reinterpret_cast<void*>(0x00433E90) : nullptr;
                       entry->mThisAdjustment = static_cast<int>(Next() % 3) * 4;
                   },
                   [] {
                       return Original<uint32_t(__thiscall*)(void*, void*, void*, void*)>(0x0042BB20)(
                           Body(), ArenaPtr(kEntry), Params(), TheScript());
                   },
                   [] {
                       return static_cast<uint32_t>(Body()->RunMemberFunction(
                           reinterpret_cast<Script::CMemberFunctionEntry*>(ArenaPtr(kEntry)), Params(), TheScript()));
                   });
    ok = ok && CompareRuns(
                   "ToggleRigidBodyDebug", 30,
                   [](int v) {
                       g_pusher[0x24] = static_cast<uint8_t>(v % 3);     // 0x006939D4
                       g_pusher[0x25] = static_cast<uint8_t>(v / 3 % 3); // 0x006939D5
                   },
                   [] {
                       return Original<uint32_t(__cdecl*)(void*, void*)>(0x0042BAC0)(Params(), TheScript()) & 0xFF;
                   },
                   [] { return static_cast<uint32_t>(Obj::CFunc_ToggleRigidBodyDebug(Params(), TheScript())); });
    // The pushers: some of the 8 the object asked about (once or twice), the others other objects or none.
    ok = ok && CompareRuns(
                   "SetRigidBodyPusherReach", 200,
                   [](int) {
                       for (int i = 0; i < 8; ++i) {
                           uint32_t which = Next() % 4;
                           uint32_t object = which == 0   ? reinterpret_cast<uint32_t>(ArenaPtr(kObj))
                                             : which == 1 ? reinterpret_cast<uint32_t>(ArenaPtr(kOtherObj))
                                                          : 0;
                           std::memcpy(&g_world[0x695150 - kWorldStart + i * 12], &object, 4);
                       }
                   },
                   [] {
                       float reach = RichFloat(30.0f);
                       Original<void(__cdecl*)(void*, float)>(0x0042BD60)(ArenaPtr(kObj), reach);
                       return 0u;
                   },
                   [] {
                       float reach = RichFloat(30.0f);
                       Obj::SetRigidBodyPusherReach(reinterpret_cast<Obj::CCompositeObject*>(ArenaPtr(kObj)), reach);
                       return 0u;
                   });
    Restore();
    Check(ok, "rigid body: RigidBody_IgnoreSkater / Disable / Reset, RegisterMemberFunctions, the components' "
              "RunMemberFunction, ToggleRigidBodyDebug, SetRigidBodyPusherReach (original vs ours)");
}

void TestRigidBodyWorld()
{
    Save();
    StandIns standIns(
        {GetComponent(),
         {0x00461410, 16, 4, Ret::Int, true, 0x8, {Ours(&Obj::CObject::SpawnScript)},
          [](Frame&, Result& r) { r.eax = reinterpret_cast<uint32_t>(ArenaPtr(kMine + 0x700)); }},
         {0x004750B0, 0, 0, Ret::Int, true, 0, {Ours(&Script::CScript::Update)}}});
    auto setup = [](int) {
        RandomBody();
        if (Next() % 2)
            g_components.push_back({kObj, 0x07713C7B, kSound});
    };
    bool ok = ComparePlain("RigidBody Finalize", 0x0042BD90, &CRigidBodyComponent::Finalize, 300, setup);
    ok = ok && ComparePlain("RigidBody Sleep", 0x0042BE90, &CRigidBodyComponent::Sleep, 40, setup);
    ok = ok && ComparePlain("RigidBody UpdateWorldPoints", 0x0042BF00, &CRigidBodyComponent::UpdateWorldPoints, 600,
                            setup);
    // Damping: drags of 0, NaN, exactly the speed; velocities with NaNs.
    ok = ok && ComparePlain("RigidBody Damp", 0x0042C6D0, &CRigidBodyComponent::Damp, 3000, [](int) {
        RandomBody();
        for (uint32_t base : {0x120u, 0x140u}) {
            for (uint32_t i = 0; i < 16; i += 4)
                ArenaAt<float>(kComp + base + i) = Next() % 20 ? RichFloat(80.0f) : Odd(80.0f);
            if (Next() % 6 == 0) // (a speed of exactly 3 or 5)
                ArenaAt<float>(kComp + base + 0) = 3.0f, ArenaAt<float>(kComp + base + 4) = 4.0f,
                                                ArenaAt<float>(kComp + base + 8) = 0.0f;
        }
        for (uint32_t at : {0x38u, 0x3Cu}) {
            switch (Next() % 6) {
            case 0: ArenaAt<float>(kComp + at) = 0.0f; break;
            case 1: ArenaAt<float>(kComp + at) = std::nanf(""); break;
            case 2: ArenaAt<float>(kComp + at) = 5.0f; break;
            default: ArenaAt<float>(kComp + at) = std::fabs(RichFloat(60.0f)); break;
            }
        }
    });
    // Sleeping: settled frames about 3, speeds about the sleep points (exactly on them too), dying or not, the
    // distance from where it woke about 48 (exactly too).
    ok = ok && ComparePlain("RigidBody CheckSleep", 0x0042C890, &CRigidBodyComponent::CheckSleep, 3000, [](int) {
        RandomBody();
        for (uint32_t base : {0x120u, 0x140u}) {
            for (uint32_t i = 0; i < 16; i += 4)
                ArenaAt<float>(kComp + base + i) = Next() % 20 ? RichFloat(4.0f) : Odd(4.0f);
        }
        ArenaAt<float>(kComp + 0x58) = Next() % 4 ? 4.0f : RichFloat(20.0f);
        ArenaAt<float>(kComp + 0x5C) = Next() % 4 ? 4.0f : RichFloat(20.0f);
        if (Next() % 4 == 0) {
            ArenaAt<float>(kComp + 0x120) = 2.0f, ArenaAt<float>(kComp + 0x124) = 0.0f,
                                       ArenaAt<float>(kComp + 0x128) = 0.0f; // (exactly the point)
        }
        for (uint32_t i = 0; i < 16; i += 4)
            ArenaAt<float>(kComp + 0xF8 + i) = ArenaAt<float>(kComp + 0x110 + i) + RichFloat(60.0f);
        if (Next() % 4 == 0) {
            for (uint32_t i = 0; i < 16; i += 4)
                ArenaAt<float>(kComp + 0xF8 + i) = ArenaAt<float>(kComp + 0x110 + i) = 10.0f;
            ArenaAt<float>(kComp + 0xF8) = 58.0f; // (exactly 48 away)
        }
        ArenaAt<float>(kComp + 0x108) = -1.0f;
    });
    Restore();
    Check(ok, "rigid body: Finalize, Sleep, UpdateWorldPoints, Damp, CheckSleep (original vs ours)");
}

// The pushers: up to 8 objects (kOtherObj and kPushers + 0x110 each) about the body, moving at it or not.
constexpr uint32_t kPushers = kMine + 0x800; // 7 more objects (0x110 apart, to kMine + 0xF70)
constexpr uint32_t kCollideSound = kMine + 0xF80;
uint32_t PusherObject(int i) { return i == 0 ? kOtherObj : kPushers + (i - 1) * 0x110; }

void RandomPushers()
{
    RandomBody();
    ArenaAt<uint32_t>(kObj + 0x14) = Next();                                  // its id
    ArenaAt<float>(kComp + 0x18) = Next() % 3 ? 0.003f : RichFloat(0.01f);   // mass over moment
    ArenaAt<float>(kComp + 0x40) = Next() % 3 ? 48.0f : std::fabs(RichFloat(60.0f));
    ArenaAt<float>(kComp + 0x44) = Next() % 3 ? 12.0f : std::fabs(RichFloat(20.0f));
    ArenaAt<float>(kComp + 0x48) = 0.906307817f;
    ArenaAt<float>(kComp + 0x4C) = 0.42261827f;
    ArenaAt<float>(kComp + 0x50) = Next() % 2 ? 1.0f : RichFloat(3.0f);
    ArenaAt<float>(kComp + 0x54) = Next() % 10 == 0 ? INFINITY : Next() % 2 ? 1.0f : RichFloat(3.0f);
    ArenaAt<float>(kComp + 0xC0) = RichFloat(1.0f);
    ArenaAt<uint32_t>(kComp + 0xC4) = Next() % 2 ? Next() : 0; // CollideScript
    ArenaAt<uint32_t>(kComp + 0xD8) = Next() % 4 ? reinterpret_cast<uint32_t>(ArenaPtr(kCollideSound)) : 0;
    ArenaAt<uint32_t>(kComp + 0x19C) = Next() % 4 ? reinterpret_cast<uint32_t>(ArenaPtr(kSound)) : 0;
    ArenaAt<int>(kComp + 0xE0) = static_cast<int>(Next() % 2000) - (Next() % 8 == 0 ? 3000 : 0); // (negative: divided unsigned)
    ArenaAt<int>(kComp + 0xE4) = static_cast<int>(Next() % 300);
    ArenaAt<int>(kComp + 0xF0) = static_cast<int>(Next() % 3000);
    ArenaAt<int>(kComp + 0x10C) = static_cast<int>(Next() % 3);
    for (uint32_t i = 0; i < 16; i += 4)
        ArenaAt<float>(kComp + 0x120 + i) = RichFloat(100.0f);
    float pos[3] = {RichFloat(300.0f), RichFloat(300.0f), RichFloat(300.0f)};
    for (int k = 0; k < 3; ++k)
        ArenaAt<float>(kComp + 0x110 + 4 * k) = pos[k];
    // The globals: the reaches, the height, the next sound time.
    for (int i = 0; i < 8; ++i) {
        float reach = Next() % 3 ? 0.0f : RichFloat(20.0f);
        std::memcpy(&g_pusher[i * 4], &reach, 4);
    }
    float height = Next() % 4 ? 77.0f : RichFloat(100.0f);
    std::memcpy(&g_pusher[0x20], &height, 4);
    int soundTime = static_cast<int>(Next() % 3000);
    std::memcpy(&g_pusher[0x28], &soundTime, 4);
    for (int i = 0; i < 8; ++i) {
        uint32_t object = 0;
        if (Next() % 3) {
            uint32_t o = PusherObject(i);
            object = reinterpret_cast<uint32_t>(ArenaPtr(o));
            // Near (often within 48 across, from 90 below to 15 above) or anywhere.
            float range = Next() % 4 ? 50.0f : 200.0f;
            ArenaAt<float>(o + 0x4C) = pos[0] + RichFloat(range);
            ArenaAt<float>(o + 0x50) = pos[1] + RichFloat(range);
            ArenaAt<float>(o + 0x54) = pos[2] + RichFloat(range);
            if (Next() % 10 == 0) // (straight above or below: no horizontal offset)
                ArenaAt<float>(o + 0x4C) = pos[0], ArenaAt<float>(o + 0x54) = pos[2];
            for (uint32_t k = 0; k < 16; k += 4) {
                float speed = Next() % 5 == 0 ? 0.0f : RichFloat(Next() % 2 ? 30.0f : 400.0f);
                ArenaAt<float>(o + 0xAC + k) = ArenaAt<float>(kComp + 0x120 + k) + speed;
            }
        }
        std::memcpy(&g_world[0x695150 - kWorldStart + i * 12], &object, 4);
    }
    if (Next() % 3 == 0) {
        // Exact cases: the body at rest at (100, 20, -40), no reaches, radius 48, application radius 12, height 77;
        // the pushers straight along -x (on the radius, or inside it) at heights on and inside the band's edges,
        // moving exactly: straight back at it, across it, up, fast.
        const float at[3] = {100.0f, 20.0f, -40.0f};
        for (int k = 0; k < 3; ++k)
            ArenaAt<float>(kComp + 0x110 + 4 * k) = at[k];
        for (uint32_t k = 0; k < 16; k += 4)
            ArenaAt<float>(kComp + 0x120 + k) = k == 12 ? 1.0f : 0.0f;
        ArenaAt<float>(kComp + 0x40) = 48.0f;
        ArenaAt<float>(kComp + 0x44) = 12.0f;
        std::memset(g_pusher.data(), 0, 0x20);
        float height = 77.0f;
        std::memcpy(&g_pusher[0x20], &height, 4);
        static const float kVel[][3] = {{-5.0f, 300.0f, 0.0f}, {0.0f, 0.0f, 5.0f}, {20.0f, 0.0f, 0.0f},
                                        {0.0f, 60.0f, 0.0f}, {200.0f, 0.0f, 0.0f}, {75.0f, 0.0f, 0.0f},
                                        {40.0f, -90.0f, 0.0f}};
        static const float kDy[] = {0.0f, 12.0f, 5.0f, -89.0f, -77.0f, -80.0f, 13.0f};
        for (int i = 0; i < 8; ++i) {
            uint32_t object = 0;
            std::memcpy(&object, &g_world[0x695150 - kWorldStart + i * 12], 4);
            if (!object)
                continue;
            uint32_t o = PusherObject(i);
            ArenaAt<float>(o + 0x4C) = at[0] - (Next() % 2 ? 48.0f : 30.0f);
            ArenaAt<float>(o + 0x50) = at[1] + kDy[Next() % 7];
            ArenaAt<float>(o + 0x54) = at[2];
            const float* v = kVel[Next() % 7];
            for (int k = 0; k < 3; ++k)
                ArenaAt<float>(o + 0xAC + 4 * k) = v[k];
        }
    }
}

void TestRigidBodyPushers()
{
    Save();
    using AddFloatFn = void (Script::CStruct::*)(uint32_t, float);
    StandIns standIns(
        {{0x00461410, 16, 4, Ret::Int, true, 0x8, {Ours(&Obj::CObject::SpawnScript)},
          [](Frame&, Result& r) { r.eax = reinterpret_cast<uint32_t>(ArenaPtr(kMine + 0x700)); }},
         {0x004750B0, 0, 0, Ret::Int, true, 0, {Ours(&Script::CScript::Update)}},
         {0x0048C770, 0, 0, Ret::Int, false, 0, {},
          [](Frame&, Result& r) { r.eax = reinterpret_cast<uint32_t>(ArenaPtr(kMine + 0x740)); }},
         {0x0049CB00, 4, 1, Ret::Int, true, 0, {},
          [](Frame&, Result& r) { r.eax = Next() % 2 ? reinterpret_cast<uint32_t>(ArenaPtr(kMine + 0x780)) : 0; }},
         {0x0049E9B0, 4, 1, Ret::None, true, 0x1},
         {0x005BE970, 0, 0, Ret::Int, false, 0, {Ours(&Tmr::GetTime)},
          [](Frame&, Result& r) { r.eax = Next() % 4000; }},
         {0x00403DE0, 0, 1, Ret::Int, false, 0, {Ours(&Mth::Rnd)},
          [](Frame& f, Result& r) { r.eax = f.args[0] ? Next() % f.args[0] : 0; }},
         {0x00477C60, 8, 2, Ret::None, true, 0, {Ours(static_cast<AddFloatFn>(&Script::CStruct::AddFloat))}},
         {0x00433240, 4, 1, Ret::None, true, 0, {Ours(&Obj::CSoundComponent::PlaySound)}}});
    bool ok = ComparePlain("RigidBody FeelPushers", 0x0042C140, &CRigidBodyComponent::FeelPushers, 20000,
                           [](int) { RandomPushers(); });
    Restore();
    Check(ok, "rigid body: FeelPushers (original vs ours)");
}

void TestRigidBodyMath()
{
    bool ok = true;
    g_rng = 77;
    for (int i = 0; i < 20000 && ok; ++i) {
        float v[4] = {Odd(30.0f), Odd(30.0f), Odd(30.0f), Odd(30.0f)};
        if (i % 50 == 0)
            v[0] = v[1] = v[2] = 0.0f;
        float length = Odd(10.0f);
        float a[4], b[4];
        std::memcpy(a, v, 16);
        std::memcpy(b, v, 16);
        auto* ra = Original<void*(__thiscall*)(void*, float)>(0x0042BBF0)(a, length);
        auto* rb = reinterpret_cast<Mth::Vector*>(b)->SetLength(length);
        ok = ra == a && reinterpret_cast<void*>(rb) == b && std::memcmp(a, b, 16) == 0;
        if (!ok)
            Log("  SetLength: case %d differs", i);
    }
    for (int i = 0; i < 20000 && ok; ++i) {
        float q[4] = {Odd(2.0f), Odd(2.0f), Odd(2.0f), Odd(2.0f)};
        if (i % 50 == 0)
            q[0] = q[1] = q[2] = q[3] = 0.0f;
        float ma[16], mb[16];
        for (int k = 0; k < 16; ++k)
            ma[k] = mb[k] = RichFloat(5.0f);
        auto* ra = Original<void*(__thiscall*)(const void*, void*)>(0x0042BC40)(q, ma);
        auto* rb = reinterpret_cast<const Mth::Quat*>(q)->GetMatrix(reinterpret_cast<Mth::Matrix*>(mb));
        ok = ra == ma && reinterpret_cast<void*>(rb) == mb && std::memcmp(ma, mb, 64) == 0;
        if (!ok)
            Log("  Quat::GetMatrix: case %d differs", i);
    }
    Check(ok, "Vector::SetLength, Quat::GetMatrix (original vs ours; zeros, NaNs)");
}

} // namespace

SELFTEST(Early, TestRigidBodyMembers);
SELFTEST(Early, TestRigidBodyWorld);
SELFTEST(Early, TestRigidBodyPushers);
SELFTEST(Early, TestRigidBodyMath);
