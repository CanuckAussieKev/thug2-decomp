// Self-tests for the lockobj component's construction, destruction and the lock's update
// (src/game/obj/lock_obj_update.cpp: 0x0041FF20, 0x0041F500, 0x0041F400, 0x0041F440, 0x0041F760) and the model
// component's quaternion and vector rotations (src/game/mth/quat.cpp: 0x00420140, 0x00420A50, 0x00420A90,
// 0x00420AC0, 0x00420B50, 0x00421440): the original and ours, every call into the rest of the game stood in for and
// recorded (the object lookup, the components, the skeleton's bones, the model's matrix, the clock, memory).

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/quat.h"
#include "game/mth/vector.h"
#include "game/obj/lock_obj.h"
#include "game/obj/model_component.h"
#include "game/obj/object.h"
#include "game/obj/objman.h"
#include "game/sys/timer.h"
#include "hook/appmain_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace AppMainTests;
using Obj::CLockObjComponent;

struct World {
    alignas(16) uint8_t component[0x90];
    alignas(16) uint8_t object[0x110];
    alignas(16) uint8_t target[0x110];
    alignas(16) uint8_t skeletonComponent[0x20];
    alignas(16) uint8_t skeleton[0x20];
    alignas(16) uint8_t model[0x240];
    alignas(16) uint8_t manager[0x40];
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

constexpr uint32_t kSkeleton = 0x222756D5;
constexpr uint32_t kModel = 0x286A8D26;

struct Answers {
    uint32_t seed;
    bool callocFails;
    uint32_t targetBy;   // 0 none, 1 by id, 2 by name
    bool haveSkeleton;   // the skeleton component
    bool haveSkeleton2;  // its skeleton
    bool haveModel;
    uint32_t boneFound;  // bit 0: the bone, bit 1: its parent
    uint32_t time;
};
Answers g_a;

void FillMatrix(float* m, uint32_t salt)
{
    Lcg random(0x4D470000 + salt);
    for (int i = 0; i < 16; ++i)
        m[i] = static_cast<float>(static_cast<int>(random.Below(2001)) - 1000) / 37.0f;
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
    s.At({0x004619F0, AddressOf(&Obj::CBaseManager::GetObjectByID)}, 4, [](Regs& r) {
        uint32_t id = r.args[0];
        Record({0x19F0, Rel(r.ecx), id});
        bool hit = (g_a.targetBy == 1 && id == 0x1D00 + g_a.seed) || (g_a.targetBy == 2 && id == 0x2A00 + g_a.seed);
        return hit ? reinterpret_cast<uint32_t>(g_world.target) : 0u;
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Record({0xDB90, Rel(r.ecx), r.args[0]});
        if (r.args[0] == kSkeleton && g_a.haveSkeleton)
            return reinterpret_cast<uint32_t>(g_world.skeletonComponent);
        if (r.args[0] == kModel && g_a.haveModel)
            return reinterpret_cast<uint32_t>(g_world.model);
        return 0u;
    });
    s.At({0x004A36E0}, 8, [](Regs& r) { // the skeleton's bone matrix
        Record({0x36E0, Rel(r.ecx), r.args[0], Rel(r.args[1])});
        // (the parent's matrix is filled even when "not found": the original then uses what its stack held)
        bool parent = r.args[0] == 0xBA0000u + g_a.seed;
        if (parent)
            FillMatrix(reinterpret_cast<float*>(r.args[1]), g_a.seed * 4 + 1);
        if (!(g_a.boneFound & (parent ? 2 : 1)))
            return 0u;
        FillMatrix(reinterpret_cast<float*>(r.args[1]), g_a.seed * 4 + (parent ? 1 : 0));
        return 1u;
    });
    s.At({0x004A2B60}, 4, [](Regs& r) { // the bone's index
        Record({0x2B60, Rel(r.ecx), r.args[0]});
        return 3u + g_a.seed % 5;
    });
    s.At({0x004A2C50}, 4, [](Regs& r) { // a bone's parent (from the skeleton's data)
        Record({0x2C50, r.ecx, r.args[0]});
        return 0xBA0000u + g_a.seed;
    });
    s.At({0x004215F0, AddressOf(&Obj::CModelComponent::GetDisplayMatrix)}, 4, [](Regs& r) { // the model's matrix
        Record({0x15F0, Rel(r.ecx), Rel(r.args[0])});
        FillMatrix(reinterpret_cast<float*>(r.args[0]), g_a.seed * 4 + 2);
        return r.args[0];
    });
    // The matrix product (0x00404100: the original's goes through D3DXMatrixMultiply, ours through Mth::Multiply - a
    // few units in the last place apart, src/hook/selftest.cpp compares them with a tolerance): both through the
    // original's D3DXMatrixMultiply here, recorded.
    using ProductFn = Mth::Matrix& (Mth::Matrix::*)(const Mth::MatrixProduct&);
    s.At({0x00404100, AddressOf(static_cast<ProductFn>(&Mth::Matrix::operator=))}, 4, [](Regs& r) {
        const auto* product = reinterpret_cast<const Mth::MatrixProduct*>(r.args[0]);
        Record({0x4100, Rel(r.ecx), Rel(reinterpret_cast<uint32_t>(product->left)),
                Rel(reinterpret_cast<uint32_t>(product->right))});
        GameFunction<void*(__stdcall*)(void*, const void*, const void*)>(0x005BFA8C)(
            reinterpret_cast<void*>(r.ecx), product->left, product->right);
        return r.ecx;
    });
    s.At({0x005BE970, AddressOf(Tmr::GetTime)}, 0, [](Regs&) {
        Record({0xE970});
        return g_a.time;
    });
}

void ResetWorld(uint32_t seed)
{
    Lcg random(0x10C50000 + seed);
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
    auto* c = reinterpret_cast<CLockObjComponent*>(g_world.component);
    c->m_object = reinterpret_cast<Obj::CCompositeObject*>(g_world.object);
    c->m_targetId = 0x1D00 + seed;
    c->m_objectName = 0x2A00 + seed;
    c->m_bone = seed % 3 == 0 ? 0u : 0xB0000u + seed;
    c->m_lockFlags = static_cast<uint8_t>(random.Next());
    c->m_phase = static_cast<int>(random.Below(1024));
    c->m_lagMin = 0.125f * static_cast<float>(seed % 7);
    c->m_lagMax = 2.5f - 0.0625f * static_cast<float>(seed);
    c->m_wobble[0] = 0.37f * static_cast<float>(seed % 5);
    c->m_wobble[1] = 1.73f + 0.5f * static_cast<float>(seed % 4);
    c->m_lag = 0.25f + 0.03125f * static_cast<float>(seed);
    reinterpret_cast<Obj::CCompositeObject*>(g_world.target)->m_id = seed % 4 == 0 ? 3u : 8u + seed % 3 * 0x7FFFFFF;
    *reinterpret_cast<void**>(g_world.skeletonComponent + 0x18) = g_a.haveSkeleton2 ? g_world.skeleton : nullptr;
    *reinterpret_cast<uint32_t*>(g_world.skeleton + 8) = 0xDA7A0000 + seed;
    g_world.model[0x234] = static_cast<uint8_t>(seed % 5 == 0 ? 0 : seed & 1);
}

constexpr uint32_t kLockAddresses[] = {0x0041FF20, 0x0041F500, 0x0041F400, 0x0041F440, 0x0041F760};

// Create (out of memory too), Construct, the destructors; the lock's update with the target found by id, by name
// or not, on a bone (the skeleton component, its skeleton, the bone and its parent there or not) or not, the model
// followed (+0x234, a skater's id) or the display matrix, with a lag (the wobble) or not, Angles.
void TestLockObjUpdate()
{
    StandIns& s = *new StandIns();
    StandIn(s);
    GameMemory memory({{0x006F0FB8, 4}});
    for (uint32_t seed = 1; seed <= 64; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            g_a = {};
            g_a.seed = seed;
            Lcg pick(0x10CC0000 + seed);
            g_a.haveSkeleton = pick.Below(5) != 0;
            g_a.haveSkeleton2 = pick.Below(5) != 0;
            g_a.haveModel = pick.Below(3) != 0;
            g_a.boneFound = pick.Below(4) == 0 ? 1 : 3;
            if (pick.Below(6) == 0)
                g_a.boneFound = 0;
            g_a.targetBy = seed % 7 == 0 ? 0 : 1 + pick.Below(2);
            g_a.time = pick.Next();
            ResetWorld(seed);
            GameVariable<void*>(0x006F0FB8) = g_world.manager;
            std::vector<OriginalCode*> guards;
            if (original) {
                for (uint32_t address : kLockAddresses)
                    guards.push_back(new OriginalCode(address));
            }
            std::vector<uint32_t> trace;
            auto* c = reinterpret_cast<CLockObjComponent*>(g_world.component);
            // Made, destroyed.
            g_a.callocFails = seed == 5;
            trace.push_back(Rel(reinterpret_cast<uint32_t(__cdecl*)()>(Code(original, 0x0041FF20))()));
            g_a.callocFails = false;
            for (size_t i = 0; i < sizeof(CLockObjComponent) / 4; ++i)
                trace.push_back(Rel(reinterpret_cast<uint32_t*>(g_world.component)[i]));
            g_world.component[0x8C] = static_cast<uint8_t>(seed * 37);
            trace.push_back(Rel(reinterpret_cast<uint32_t(__thiscall*)(void*)>(Code(original, 0x0041F500))(c)));
            trace.push_back(g_world.component[0x8C]);
            *reinterpret_cast<uint32_t*>(g_world.component) = 0xD1D1D1D1; // (a derived class's vtable: put back)
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x0041F400))(c);
            *reinterpret_cast<uint32_t*>(g_world.component) = 0xD2D2D2D2;
            trace.push_back(Rel(reinterpret_cast<uint32_t(__thiscall*)(void*, unsigned)>(Code(original, 0x0041F440))(
                c, seed % 3)));
            // Updates.
            ResetWorld(seed);
            for (int k = 0; k < 3; ++k) {
                reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x0041F760))(c);
                g_a.time += 0x9E3779B9u;
                if (k == 1)
                    c->m_lockFlags ^= Obj::LOCK_FLAG_LAG;
            }
            for (OriginalCode* guard : guards)
                delete guard;
            // (the state first: fixed places in the trace)
            for (size_t i = 0; i < sizeof(CLockObjComponent) / 4; ++i)
                trace.push_back(Rel(reinterpret_cast<uint32_t*>(g_world.component)[i]));
            for (size_t i = 0; i < sizeof(g_world.object) / 4; ++i)
                trace.push_back(Rel(reinterpret_cast<uint32_t*>(g_world.object)[i]));
            for (size_t i = 0; i < sizeof(g_world.target) / 4; ++i)
                trace.push_back(Rel(reinterpret_cast<uint32_t*>(g_world.target)[i]));
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "lockobj: made, destroyed, the lock's update");
    }
    delete &s;
}

constexpr uint32_t kQuatAddresses[] = {0x00420140, 0x00420A50, 0x00420A90, 0x00420AC0, 0x00420B50, 0x00421440};

// The quaternion and vector rotations on random values (zero axes, zero and large angles, NaN, infinity).
void TestModelRotations()
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (uint32_t seed = 1; seed <= 200; ++seed) {
        auto run = [&](bool original) {
            Lcg random(0x0A7E0000 + seed);
            auto value = [&random, nan, inf](int kind) {
                switch (kind) {
                case 0: return 0.0f;
                case 1: return nan;
                case 2: return -inf;
                default: return static_cast<float>(static_cast<int>(random.Below(20001)) - 10000) / 997.0f;
                }
            };
            auto pickKind = [&random, seed]() { return seed % 9 == 0 ? static_cast<int>(random.Below(12)) : 3; };
            alignas(16) float a[4], b[4], out[4];
            for (float& f : a)
                f = value(pickKind());
            for (float& f : b)
                f = value(pickKind());
            if (seed % 11 == 0)
                a[0] = a[1] = a[2] = 0.0f; // (a zero axis)
            float angle = seed % 13 == 0 ? 0.0f : value(pickKind()) * (seed % 5 == 0 ? 1000.0f : 1.0f);
            std::vector<OriginalCode*> guards;
            if (original) {
                for (uint32_t address : kQuatAddresses)
                    guards.push_back(new OriginalCode(address));
            }
            std::vector<uint32_t> trace;
            // (a NaN's sign isn't compared: the x87 and our builds pick different signs for the NaN they pass on)
            auto push = [&trace](const float* f) {
                for (int i = 0; i < 4; ++i)
                    trace.push_back(f[i] != f[i] ? 0x7FC00000u : Bits(f[i]));
            };
            float q[4];
            std::memcpy(q, a, sizeof(q));
            trace.push_back(Rel(reinterpret_cast<uint32_t(__thiscall*)(float*, const float*)>(Code(original, 0x00420140))(
                q, b)));
            push(q);
            std::memcpy(q, a, sizeof(q));
            reinterpret_cast<void(__thiscall*)(float*, float)>(Code(original, 0x00420A50))(q, angle);
            push(q);
            std::memcpy(q, a, sizeof(q));
            reinterpret_cast<void(__thiscall*)(float*, float)>(Code(original, 0x00420A90))(q, angle);
            push(q);
            std::memset(out, 0xCD, sizeof(out));
            trace.push_back(Rel(reinterpret_cast<uint32_t(__thiscall*)(float*, const float*, float)>(
                Code(original, 0x00420AC0))(out, a, angle)));
            push(out);
            std::memset(out, 0xCD, sizeof(out));
            trace.push_back(Rel(reinterpret_cast<uint32_t(__thiscall*)(const float*, float*, const float*)>(
                Code(original, 0x00420B50))(a, out, b)));
            push(out);
            std::memcpy(q, b, sizeof(q));
            trace.push_back(Rel(reinterpret_cast<uint32_t(__thiscall*)(float*, const float*, float)>(
                Code(original, 0x00421440))(q, a, angle)));
            push(q);
            for (OriginalCode* guard : guards)
                delete guard;
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "model rotations: quaternions (product, axis-angle, rotating a vector), about X / Y / an axis");
    }
}

} // namespace

SELFTEST(Early, TestLockObjUpdate, RemoveStandIns);
SELFTEST(Early, TestModelRotations);
