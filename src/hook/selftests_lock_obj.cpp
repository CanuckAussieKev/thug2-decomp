// Self-tests for the lockobj component (src/game/obj/lock_obj.cpp: 0x0041F410, 0x0041F520, 0x0041FF90): the original
// and ours, every call they make stood in for and recorded (the script structure's getters, the object lookup, the
// manager's priority slot, Rnd, the clock, the angles and their matrix, the lock's update); and THAW's no_rotate (not
// in the original) against what it should do.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/mth/random.h"
#include "game/obj/lock_obj.h"
#include "game/obj/object.h"
#include "game/obj/objman.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "hook/appmain_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace AppMainTests;

struct World {
    alignas(16) uint8_t component[0x90];
    alignas(16) uint8_t object[0x110];
    alignas(16) uint8_t target[0x110];
    alignas(16) uint8_t manager[0x40];
    void* managerVtable[8];
    void* componentVtable[16];
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

constexpr uint32_t kObjectName = 0x2BFC2486;
constexpr uint32_t kId = 0x40C698AF;
constexpr uint32_t kBone = 0xCAB94088;
constexpr uint32_t kBoneOffset = 0xBE9AB709;
constexpr uint32_t kOff = 0xD443A2BC;
constexpr uint32_t kLagRange = 0x700C4085;
constexpr uint32_t kLagWobble = 0xD25807F0;
constexpr uint32_t kAngles = 0x9D2D0915;
constexpr uint32_t kNoRotate = 0xD74A6981;

// What the stand-ins answer this run (bits of `params`: which parameters the structure has).
struct Answers {
    uint32_t seed;
    uint32_t params;
    uint32_t targetBy; // 0 none, 1 by id, 2 by name
    uint32_t time;
};
Answers g_a;
enum : uint32_t {
    P_OFF = 1,
    P_NAME = 2,
    P_VECTOR = 4,
    P_BONE_OFFSET = 8,
    P_ID = 0x10,
    P_BONE = 0x20,
    P_LAG_RANGE = 0x40,
    P_WOBBLE = 0x80,
    P_ANGLES = 0x100,
    P_NO_ROTATE = 0x200,
};
bool Has(uint32_t p) { return (g_a.params & p) != 0; }

void StandIn(StandIns& s)
{
    using GetChecksumFn = bool (Script::CStruct::*)(uint32_t, uint32_t*, int);
    s.At({0x00476950, AddressOf(static_cast<GetChecksumFn>(&Script::CStruct::GetChecksum))}, 12, [](Regs& r) {
        uint32_t name = r.args[0];
        auto* out = reinterpret_cast<uint32_t*>(r.args[1]);
        Record({0x6950, r.ecx, name, Rel(r.args[1]), *out, r.args[2]});
        bool found = name == kObjectName ? Has(P_NAME) : name == kId ? Has(P_ID) : name == kBone ? Has(P_BONE) : false;
        if (!found)
            return 0u;
        *out = name ^ g_a.seed;
        return 1u;
    });
    using GetVectorFn = bool (Script::CStruct::*)(uint32_t, Script::VectorValue*, int);
    s.At({0x00476FB0, AddressOf(static_cast<GetVectorFn>(&Script::CStruct::GetVector))}, 12, [](Regs& r) {
        uint32_t name = r.args[0];
        Record({0x6FB0, r.ecx, name, Rel(r.args[1]), r.args[2]});
        bool found = name == 0 ? Has(P_VECTOR) : name == kBoneOffset ? Has(P_BONE_OFFSET) : false;
        if (!found)
            return 0u;
        auto* out = reinterpret_cast<float*>(r.args[1]);
        out[0] = static_cast<float>(g_a.seed) * 1.5f + (name ? 100.0f : 0.0f);
        out[1] = -2.25f * static_cast<float>(g_a.seed);
        out[2] = 576.24f;
        out[3] = 1.0f;
        return 1u;
    });
    using GetPairFn = bool (Script::CStruct::*)(uint32_t, float*, float*, int);
    s.At({0x00476770, AddressOf(static_cast<GetPairFn>(&Script::CStruct::GetPair))}, 16, [](Regs& r) {
        uint32_t name = r.args[0];
        auto* x = reinterpret_cast<float*>(r.args[1]);
        auto* y = reinterpret_cast<float*>(r.args[2]);
        Record({0x6770, r.ecx, name, Rel(r.args[1]), Rel(r.args[2]), Bits(*x), Bits(*y), r.args[3]});
        if (name == kLagRange && Has(P_LAG_RANGE)) {
            *x = 0.25f + 0.0625f * static_cast<float>(g_a.seed);
            *y = 3.0f - 0.125f * static_cast<float>(g_a.seed);
            return 1u;
        }
        if (name == kLagWobble && Has(P_WOBBLE)) {
            *x = 0.37f * static_cast<float>(g_a.seed + 1);
            *y = 1.73f + 0.5f * static_cast<float>(g_a.seed);
            return 1u;
        }
        return 0u;
    });
    using FlagFn = bool (Script::CStruct::*)(uint32_t);
    s.At({0x00476B40, AddressOf(static_cast<FlagFn>(&Script::CStruct::ContainsFlag))}, 4, [](Regs& r) {
        // (ours alone asks for no_rotate: not recorded - TestLockObjNoRotate checks it)
        if (r.args[0] != kNoRotate)
            Record({0x6B40, r.ecx, r.args[0]});
        return r.args[0] == kOff ? Has(P_OFF) : r.args[0] == kNoRotate ? Has(P_NO_ROTATE) : false;
    });
    s.At({0x00476AF0, AddressOf(static_cast<FlagFn>(&Script::CStruct::ContainsComponentNamed))}, 4, [](Regs& r) {
        Record({0x6AF0, r.ecx, r.args[0]});
        return r.args[0] == kAngles && Has(P_ANGLES) ? 1u : 0u;
    });
    s.At({0x004619F0, AddressOf(&Obj::CBaseManager::GetObjectByID)}, 4, [](Regs& r) {
        uint32_t id = r.args[0];
        Record({0x19F0, Rel(r.ecx), id});
        bool hit = (g_a.targetBy == 1 && id == (kId ^ g_a.seed)) || (g_a.targetBy == 2 && id == (kObjectName ^ g_a.seed)) ||
                   (g_a.targetBy == 3 && id == 0);
        return hit ? reinterpret_cast<uint32_t>(g_world.target) : 0u;
    });
    g_world.managerVtable[4] = s.Thunk(8, [](Regs& r) {
        Record({0x4004, Rel(r.ecx), Rel(r.args[0]), r.args[1]});
        return 0u;
    });
    s.At({0x00403DE0, AddressOf(Mth::Rnd)}, 0, [](Regs& r) {
        Record({0x3DE0, r.args[0]});
        return (g_a.seed * 337u + 5u) % r.args[0];
    });
    s.At({0x005BE970, AddressOf(Tmr::GetTime)}, 0, [](Regs&) {
        Record({0xE970});
        return g_a.time;
    });
    s.At({0x0045C180, AddressOf(static_cast<bool (*)(Script::CStruct*, Mth::Vector*)>(&SkateScript::GetAngles))}, 0,
         [](Regs& r) {
             Record({0xC180, r.args[0], Rel(r.args[1])});
             auto* out = reinterpret_cast<float*>(r.args[1]);
             out[0] = 0.1f * static_cast<float>(g_a.seed);
             out[1] = -0.5f;
             out[2] = 1.25f;
             out[3] = 1.0f;
             return 1u;
         });
    s.At({0x0041A730}, 4, [](Regs& r) {
        const auto* a = reinterpret_cast<const float*>(r.args[0]);
        Record({0xA730, Rel(r.ecx), Rel(r.args[0]), Bits(a[0]), Bits(a[1]), Bits(a[2]), Bits(a[3])});
        auto* m = reinterpret_cast<float*>(r.ecx);
        for (int i = 0; i < 16; ++i)
            m[i] = a[i % 3] * static_cast<float>(i + 1);
        return r.ecx;
    });
    // The lock's update (0x0041F760: selftests_obj_lock_update.cpp tests it): recorded; moves the object and takes
    // another orientation, as it would.
    s.At({0x0041F760, AddressOf(&Obj::CLockObjComponent::UpdateLock)}, 0, [](Regs& r) {
        auto* object = reinterpret_cast<Obj::CCompositeObject*>(g_world.object);
        Record({0xF760, Rel(r.ecx)});
        object->m_pos = {object->m_pos.x + 10.0f, object->m_pos.y - 3.0f, object->m_pos.z + 0.5f, 1.0f};
        for (int i = 0; i < 4; ++i) {
            object->m_matrix.row[i] = {static_cast<float>(i) - 7.0f, 0.5f, static_cast<float>(g_a.seed), 0.0f};
            object->m_displayMatrix.row[i] = object->m_matrix.row[i];
        }
        return 0u;
    });
    // The component's slot 2 (InitFromStructure, called virtually by CallMemberFunction).
    g_world.componentVtable[2] = s.Thunk(4, [](Regs& r) {
        Record({0x0002, Rel(r.ecx), r.args[0]});
        return 0u;
    });
}

void ResetWorld(uint32_t seed)
{
    Lcg random(0x10C40000 + seed);
    auto fill = [&random](uint8_t* p, size_t size) {
        for (size_t i = 0; i < size; i += 4) {
            float f = static_cast<float>(static_cast<int>(random.Below(4001)) - 2000) / 16.0f;
            std::memcpy(p + i, &f, 4);
        }
    };
    fill(g_world.component, sizeof(g_world.component));
    fill(g_world.object, sizeof(g_world.object));
    fill(g_world.target, sizeof(g_world.target));
    std::memset(g_world.manager, 0, sizeof(g_world.manager));
    *reinterpret_cast<void***>(g_world.manager) = g_world.managerVtable;
    auto* c = reinterpret_cast<Obj::CLockObjComponent*>(g_world.component);
    c->m_vtable = g_world.componentVtable;
    c->m_object = reinterpret_cast<Obj::CCompositeObject*>(g_world.object);
    // (the original never touches bit 8, ours clears it: compared from a clear bit; flags otherwise random)
    c->m_lockFlags = static_cast<uint8_t>(random.Next()) & ~Obj::LOCK_FLAG_NO_ROTATE;
    // (the target's node priority: some edge values)
    static const int priorities[] = {0, 5, -1000, 0x7FFFFFFF, static_cast<int>(0x80000000), 900};
    reinterpret_cast<Obj::CObject*>(g_world.target)->m_node.priority = priorities[seed % 6];
}

// InitFromStructure for every combination of parameters the seeds pick (off, the name / id / bone, the vector or
// BoneOffset, the target found by id, by name or not, LagRange, lagwobble, Angles), and CallMemberFunction.
void TestLockObjInit()
{
    StandIns& s = *new StandIns();
    StandIn(s);
    GameMemory memory({{0x006F0FB8, 4}});
    for (uint32_t seed = 1; seed <= 40; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            ResetWorld(seed);
            g_a = {};
            g_a.seed = seed;
            Lcg pick(0xB17E0000 + seed);
            g_a.params = pick.Next() & 0x1FF; // (no_rotate: TestLockObjNoRotate)
            if (seed % 4 == 0)
                g_a.params &= ~P_OFF;
            g_a.targetBy = seed % 4;
            g_a.time = pick.Next() * 977u;
            GameVariable<void*>(0x006F0FB8) = g_world.manager;
            Originals guard(original, {0x0041F410, 0x0041F520, 0x0041FF90});
            std::vector<uint32_t> trace;
            auto* c = reinterpret_cast<Obj::CLockObjComponent*>(g_world.component);
            auto* params = reinterpret_cast<Script::CStruct*>(0x5C700000 + seed);
            reinterpret_cast<void(__thiscall*)(void*, void*)>(Code(original, 0x0041F520))(c, params);
            // CallMemberFunction: Obj_LockToObject (through slot 2) and another member.
            for (uint32_t name : {0x785E57C7u, 0x785E57C6u, 0u}) {
                trace.push_back(reinterpret_cast<uint32_t(__thiscall*)(void*, uint32_t, void*, void*)>(
                    Code(original, 0x0041F410))(c, name, params, reinterpret_cast<void*>(0x5C900000)));
            }
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            for (size_t i = 0; i < sizeof(g_world.component) / 4; ++i)
                trace.push_back(Rel(reinterpret_cast<uint32_t*>(g_world.component)[i]));
            for (size_t i = 0; i < sizeof(g_world.object) / 4; ++i)
                trace.push_back(Rel(reinterpret_cast<uint32_t*>(g_world.object)[i]));
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "lockobj: Obj_LockToObject's parameters (InitFromStructure, CallMemberFunction)");
    }
    delete &s;
}

// Update: unlocked (nothing), locked (the lock's update) - the original and ours; then THAW's no_rotate (ours only):
// InitFromStructure sets the bit with no_rotate and clears it without (also when unlocking), and Update puts the
// object's own orientation back after the lock's update, keeping the position it gave.
void TestLockObjNoRotate()
{
    StandIns& s = *new StandIns();
    StandIn(s);
    GameMemory memory({{0x006F0FB8, 4}});
    for (uint32_t seed = 1; seed <= 8; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            ResetWorld(seed);
            g_a = {};
            g_a.seed = seed;
            Originals guard(original, {0x0041FF90});
            auto* c = reinterpret_cast<Obj::CLockObjComponent*>(g_world.component);
            c->m_lockFlags = static_cast<uint8_t>(seed & 1 ? Obj::LOCK_FLAG_LOCKED : 0) | static_cast<uint8_t>(seed & 6);
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x0041FF90))(c);
            std::vector<uint32_t> trace = g_trace;
            for (size_t i = 0; i < sizeof(g_world.object) / 4; ++i)
                trace.push_back(Rel(reinterpret_cast<uint32_t*>(g_world.object)[i]));
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "lockobj: Update (unlocked, locked)");
    }
    GameVariable<void*>(0x006F0FB8) = g_world.manager;
    for (uint32_t seed = 1; seed <= 6; ++seed) {
        ResetWorld(seed);
        g_a = {};
        g_a.seed = seed;
        auto* c = reinterpret_cast<Obj::CLockObjComponent*>(g_world.component);
        auto* object = reinterpret_cast<Obj::CCompositeObject*>(g_world.object);
        auto* params = reinterpret_cast<Script::CStruct*>(0x5C700000 + seed);
        // set by no_rotate, cleared without it, cleared by off
        c->m_lockFlags = seed & 1 ? Obj::LOCK_FLAG_NO_ROTATE : 0;
        g_a.params = P_NAME | P_VECTOR | (seed % 3 == 0 ? P_OFF : 0) | (seed & 2 ? P_NO_ROTATE : 0);
        c->InitFromStructure(params);
        bool locked = (c->m_lockFlags & Obj::LOCK_FLAG_LOCKED) != 0;
        bool noRotate = (c->m_lockFlags & Obj::LOCK_FLAG_NO_ROTATE) != 0;
        Check(locked == !(g_a.params & P_OFF) && noRotate == (locked && (g_a.params & P_NO_ROTATE) != 0),
              "lockobj: no_rotate set by Obj_LockToObject no_rotate, cleared without it and by off");
        // the update: the lock's position, the object's own orientation
        c->m_lockFlags = Obj::LOCK_FLAG_LOCKED | Obj::LOCK_FLAG_NO_ROTATE;
        Mth::Vector pos = object->m_pos;
        Mth::Matrix own = object->m_matrix;
        g_trace.clear();
        c->Update();
        bool called = g_trace.size() == 2 && g_trace[0] == 0xF760;
        bool moved = object->m_pos.x == pos.x + 10.0f && object->m_pos.y == pos.y - 3.0f && object->m_pos.z == pos.z + 0.5f;
        bool kept = std::memcmp(&object->m_matrix, &own, sizeof(own)) == 0 &&
                    std::memcmp(&object->m_displayMatrix, &own, sizeof(own)) == 0;
        Check(called && moved && kept, "lockobj: no_rotate keeps the object's orientation, the lock's position");
    }
    delete &s;
}

} // namespace

SELFTEST(Early, TestLockObjInit, RemoveStandIns);
SELFTEST(Early, TestLockObjNoRotate, RemoveStandIns);
