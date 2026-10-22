// Self-tests for the rigid body component's physics (game/obj/rigidbody_physics.cpp): the originals against ours on
// stand-in objects - the frame's update, integration, world collisions, contacts, springs, bounces - every call into
// code not rewritten (or not this test's) recorded by stand-ins (hook/standins.h); the component, the world arrays
// (0x006949B0-0x0069514F), the object and the heap compared afterwards. The quaternion helpers 0x00420140 /
// 0x0042BC40 (pure) run for real.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#include "game/core/manager.h"
#include "game/mth/matrix.h"
#include "game/mth/quat.h"
#include "game/nx/feeler.h"
#include "game/obj/object.h"
#include "game/obj/rigidbody_component.h"
#include "game/obj/smtptr.h"
#include "game/obj/sound_component.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sfx/sfx.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using Obj::CRigidBodyComponent;

struct Random {
    uint32_t state;
    uint32_t Next()
    {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    }
    uint32_t Below(uint32_t n) { return Next() % n; }
    bool Bit() { return (Next() & 1) != 0; }
    bool OneIn(uint32_t n) { return Below(n) == 0; }
    // A value with every bit of its mantissa used (+-2142).
    float Value() { return static_cast<float>(static_cast<int>(Below(30001)) - 15000) / 7.0f; }
    float Unit() { return static_cast<float>(Below(100001)) / 100000.0f * (1.0f + 1.0f / 3.0f) * 0.75f; }
};

template <typename T>
T& At(void* base, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset);
}
uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

constexpr uint32_t kWorldArrays = 0x006949B0, kWorldArraysSize = 0x00695150 - 0x006949B0;
constexpr int kMaxPoints = 24;

struct World {
    uint8_t component[0x1A0];
    uint8_t object[0x120];
    uint8_t other[0x120];
    uint8_t heap[0x1000];
    Mth::Vector points[kMaxPoints];
    CRigidBodyComponent::Sphere spheres[kMaxPoints];
    CRigidBodyComponent::Spring springs[2];
    CRigidBodyComponent::DirectedFriction friction[kMaxPoints];
    uint8_t structs[4][0x10];
    uint8_t sound[0x40];
    uint8_t script[0x40];
    uint8_t feeler[0xB0];
    uint32_t vtable[12];
};
World g_world;
uint32_t g_heapUsed;
double g_frameSeconds;

uint32_t Rel(uint32_t word)
{
    auto base = reinterpret_cast<uint32_t>(&g_world);
    if (word >= base && word < base + sizeof(World))
        return 0x77000000 + (word - base);
    auto* tib = reinterpret_cast<NT_TIB*>(NtCurrentTeb());
    if (word >= reinterpret_cast<uint32_t>(tib->StackLimit) && word < reinterpret_cast<uint32_t>(tib->StackBase))
        return 0x57AC0000;
    return word;
}

struct Answers {
    uint32_t hits;      // which of Update's steps hit something (bit per step)
    bool contacts;      // FindContacts' answer
    uint32_t voices;    // voices playing
    float distance[5];  // the hits' distances along the line
    bool hitObject;     // the hit holds an object
    float normal[4], point[4];
};
Answers g_a;
uint32_t g_step;

__declspec(naked) void FrameSecondsStandIn()
{
    __asm {
        fld qword ptr [g_frameSeconds]
        ret
    }
}

void* HeapBlock(uint32_t size)
{
    uint32_t aligned = (size + 15) & ~15u;
    if (g_heapUsed + aligned > sizeof(g_world.heap))
        return nullptr;
    void* p = g_world.heap + g_heapUsed;
    g_heapUsed += aligned;
    std::memset(p, 0xA7, size);
    return p;
}

void StandIn(StandIns& s)
{
    using Script::CStruct;
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Record({0xAFC0, r.args[0], r.args[1], r.args[2]});
        return reinterpret_cast<uint32_t>(HeapBlock(r.args[0]));
    });
    // The rigid body's code before this range (the original's and ours: rigidbody_support.cpp).
    using Obj::CRigidBodyComponent;
    const std::pair<uint32_t, uint32_t> before[] = {
        {0x0042C140u, AddressOf(&CRigidBodyComponent::FeelPushers)},
        {0x0042BF00u, AddressOf(&CRigidBodyComponent::UpdateWorldPoints)},
        {0x0042BE90u, AddressOf(&CRigidBodyComponent::Sleep)},
        {0x0042C6D0u, AddressOf(&CRigidBodyComponent::Damp)},
        {0x0042C890u, AddressOf(&CRigidBodyComponent::CheckSleep)}};
    for (const auto& [address, ours] : before) {
        auto record = [address](Regs& r) {
            Record({address, Rel(r.ecx)});
            return 0u;
        };
        if (ours)
            s.At({address, ours}, 0, record);
        else
            s.At({address}, 0, record);
    }
    s.At({0x0042C950, AddressOf(&CRigidBodyComponent::FindContacts)}, 0, [](Regs& r) {
        Record({0xC950, Rel(r.ecx)});
        return static_cast<uint32_t>(g_a.contacts);
    });
    // The feeler.
    s.At({0x00417760, AddressOf(static_cast<Nx::CFeeler* (Nx::CFeeler::*)()>(&Nx::CFeeler::Construct))}, 0,
         [](Regs& r) {
             Record({0x7760, Rel(r.ecx)});
             std::memset(reinterpret_cast<void*>(r.ecx), 0, 0xB0);
             return r.ecx;
         });
    s.At({0x004172F0, AddressOf(&Nx::CFeeler::GetCollision)}, 8, [](Regs& r) {
        auto* line = reinterpret_cast<const uint32_t*>(r.ecx);
        Record({0x72F0, Rel(r.ecx), r.args[0], r.args[1]});
        for (int i = 0; i < 8; ++i)
            Record({line[i]});
        bool hit = (g_a.hits >> g_step) & 1;
        if (hit) {
            At<float>(reinterpret_cast<void*>(r.ecx), 0x74) = g_a.distance[g_step % 5];
            std::memcpy(reinterpret_cast<uint8_t*>(r.ecx) + 0x78, g_a.point, 16);
            std::memcpy(reinterpret_cast<uint8_t*>(r.ecx) + 0x88, g_a.normal, 16);
        }
        if (g_a.hitObject)
            At<uint32_t>(reinterpret_cast<void*>(r.ecx), 0x98) = reinterpret_cast<uint32_t>(g_world.other);
        ++g_step;
        return static_cast<uint32_t>(hit);
    });
    s.At({0x004637E0, AddressOf(&Obj::CRefCounted::RemoveSmartPointer)}, 4, [](Regs& r) {
        Record({0x37E0, Rel(r.ecx), Rel(r.args[0])});
        return 0u;
    });
    // Scripts and structures.
    s.At({0x00478480, AddressOf(&CStruct::AppendStructure)}, 4, [](Regs& r) {
        Record({0x8480, Rel(r.ecx), Rel(r.args[0])});
        return r.ecx;
    });
    s.At({0x00477090, 0x00477130, AddressOf(&CStruct::Clear)}, 0, [](Regs& r) {
        Record({0x7090, Rel(r.ecx)});
        return 0u;
    });
    s.At({0x00409070, AddressOf(Script::DeleteStruct)}, 0, [](Regs& r) {
        Record({0x9070, Rel(r.args[0])});
        return 0u;
    });
    s.At({0x00477F70, AddressOf(static_cast<void (CStruct::*)(uint32_t, Script::VectorValue)>(&CStruct::AddVector))},
         0x14, [](Regs& r) {
             Record({0x7F70, Rel(r.ecx), r.args[0], r.args[1], r.args[2], r.args[3], r.args[4]});
             return 0u;
         });
    s.At({0x00477C60, AddressOf(static_cast<void (CStruct::*)(uint32_t, float)>(&CStruct::AddFloat))}, 8,
         [](Regs& r) {
             Record({0x7C60, Rel(r.ecx), r.args[0], r.args[1]});
             return 0u;
         });
    s.At({0x00461410, AddressOf(&Obj::CObject::SpawnScript)}, 16, [](Regs& r) {
        Record({0x1410, Rel(r.ecx), r.args[0], Rel(r.args[1]), r.args[2], r.args[3] & 0xFF});
        return reinterpret_cast<uint32_t>(g_world.script);
    });
    s.At({0x004750B0, AddressOf(&Script::CScript::Update)}, 0, [](Regs& r) {
        Record({0x50B0, Rel(r.ecx)});
        return 0u;
    });
    s.At({0x0047AFE0, AddressOf(Sfx::CountPlayingVoices)}, 0, [](Regs&) {
        Record({0xAFE0});
        return g_a.voices;
    });
    s.At({0x00433240, AddressOf(&Obj::CSoundComponent::PlaySound)}, 4, [](Regs& r) {
        Record({0x3240, Rel(r.ecx), Rel(r.args[0])});
        return 0u;
    });
    for (uint32_t address : {0x005BEAA0u, AddressOf(Tmr::GetSlomoFrameSeconds)})
        s.Bytes(address, Branch(0xE9, address, reinterpret_cast<void*>(&FrameSecondsStandIn)));
}

void Fill(Random& r, void* p, size_t size, float scale)
{
    for (size_t i = 0; i < size; i += 4)
        At<float>(p, static_cast<uint32_t>(i)) = r.Value() * scale;
}

// A direction of about unit length (or along an axis).
void Normal(Random& r, float* n)
{
    if (r.OneIn(4)) {
        n[0] = n[1] = n[2] = 0.0f;
        n[r.Below(3)] = r.Bit() ? 1.0f : -1.0f;
    } else {
        float x = r.Value(), y = r.Value(), z = r.Value();
        float length = std::sqrt(x * x + y * y + z * z);
        if (length == 0.0f)
            length = 1.0f;
        n[0] = x / length;
        n[1] = y / length;
        n[2] = z / length;
    }
    n[3] = r.OneIn(3) ? 0.0f : r.Value() / 1000.0f;
}

void Build(Random& r)
{
    g_heapUsed = 0;
    g_step = 0;
    std::memset(g_world.heap, 0xCD, sizeof(g_world.heap));
    Fill(r, g_world.component, sizeof(g_world.component), 1.0f / 50.0f);
    Fill(r, g_world.object, sizeof(g_world.object), 1.0f / 50.0f);
    Fill(r, g_world.other, sizeof(g_world.other), 1.0f / 50.0f);
    Fill(r, g_world.points, sizeof(g_world.points), 1.0f / 200.0f);
    Fill(r, g_world.spheres, sizeof(g_world.spheres), 1.0f / 200.0f);
    Fill(r, g_world.springs, sizeof(g_world.springs), 1.0f / 200.0f);
    Fill(r, g_world.friction, sizeof(g_world.friction), 1.0f / 1000.0f);
    for (auto& f : g_world.friction)
        f.active = r.Bit();
    std::memset(g_world.structs, 0, sizeof(g_world.structs));
    At<uint32_t*>(g_world.object, 0) = g_world.vtable;

    auto* c = reinterpret_cast<CRigidBodyComponent*>(g_world.component);
    c->m_object = reinterpret_cast<Obj::CCompositeObject*>(g_world.object);
    c->mp_contacts = g_world.points;
    c->m_contactCount = static_cast<uint16_t>(r.OneIn(4) ? 1 : r.Below(kMaxPoints + 1));
    c->mp_spheres = g_world.spheres;
    c->m_sphereCount = static_cast<uint16_t>(r.Below(kMaxPoints + 1));
    c->mp_springs = g_world.springs;
    c->m_springCount = static_cast<uint16_t>(r.Below(3));
    for (auto& spring : g_world.springs)
        spring.active = !r.OneIn(4);
    c->mp_directedFriction = r.Bit() ? g_world.friction : nullptr;
    c->m_massOverMoment = r.Bit() ? 0.003000000026077032f : r.Value() / 100000.0f;
    c->m_restitution = r.Unit();
    c->m_friction = r.Unit();
    c->m_springConst = r.OneIn(3) ? 10.0f : r.Value() / 100.0f;
    if (r.Bit())
        c->m_constAcc = {0.0f, -386.3999938964844f, 0.0f, 1.0f};
    c->mp_callbackParams = r.Bit() ? reinterpret_cast<Script::CStruct*>(g_world.structs[0]) : nullptr;
    c->mp_bounceSound = r.Bit() ? reinterpret_cast<Script::CStruct*>(g_world.structs[1]) : nullptr;
    c->mp_collideSound = reinterpret_cast<Script::CStruct*>(g_world.structs[2]);
    c->mp_soundComponent = r.Bit() ? reinterpret_cast<Obj::CSoundComponent*>(g_world.sound) : nullptr;
    c->m_bounceScript = r.Bit() ? 0xB0B0B0B0 : 0u;
    c->m_scriptD0 = r.Bit() ? 0x5C5C5C5C : 0u;
    c->m_bounceCallbackThreshold = r.OneIn(4) ? 20.0f : r.Value() / 20.0f;
    c->m_bounceFullSpeed = r.OneIn(4) ? 300.0f : r.Value() / 5.0f;
    c->m_flags = r.Below(4);
    c->m_state = static_cast<int>(r.Below(8) < 6 ? 1 : r.Below(3));
    c->m_unknown191 = static_cast<int8_t>(static_cast<int>(r.Below(9)) - 3);
    c->m_contactsHit = static_cast<int8_t>(r.OneIn(5) ? 0 : r.Below(kMaxPoints + 1));
    switch (r.Below(4)) {
    case 0: c->m_dieTimer = -1.0f; break;
    case 1: c->m_dieTimer = 0.0f; break;
    default: c->m_dieTimer = r.Value() / 50000.0f; break;
    }
    c->m_pushCooldown = r.OneIn(3) ? 0.0f : r.Value() / 50000.0f;
    // A unit quaternion and its matrix-ish turn (the matrix is only read).
    float q[4];
    Normal(r, q);
    c->m_orientation = {q[0] * 0.6f, q[1] * 0.6f, q[2] * 0.6f, 0.8f};
    Fill(r, &c->m_matrix, sizeof(c->m_matrix), 1.0f / 2000.0f);
    // The world arrays: contacts, the world points (as if 0x0042BF00 had set them).
    std::memset(reinterpret_cast<void*>(kWorldArrays), 0, kWorldArraysSize);
    auto* contacts = CRigidBodyComponent::Contacts();
    for (int i = 0; i < kMaxPoints; ++i) {
        auto& k = contacts[i];
        Fill(r, &k.offset, 16, 1.0f / 200.0f);
        Normal(r, &k.normal.x);
        k.impulseScale = r.OneIn(3) ? r.Value() / 1000000.0f : r.Value() / 1000.0f;
        k.impulse = r.Value() / 100.0f;
        k.depth = r.Value() / 1000.0f;
        k.active = !r.OneIn(4);
        k.scaled = r.Bit();
        k.friction = static_cast<int8_t>(r.Below(kMaxPoints));
        k.pad2F = 0;
        if (r.OneIn(6)) // (a point moving exactly along the normal: nothing across it)
            k.offset = {0.0f, 0.0f, 0.0f, 0.0f};
    }
    for (Mth::Vector* world : {CRigidBodyComponent::WorldContactPoints(), CRigidBodyComponent::WorldSphereCenters()})
        Fill(r, world, kMaxPoints * 16, 1.0f / 200.0f);
    Fill(r, CRigidBodyComponent::WorldSpringPoints(), 2 * 16, 1.0f / 200.0f);
    // Edges: a spring point exactly at its anchor; a still body; velocity along an axis.
    if (r.OneIn(6)) {
        c->m_pos = {static_cast<float>(r.Below(64)), static_cast<float>(r.Below(64)), 2.0f, 1.0f};
        for (int i = 0; i < 2; ++i) {
            CRigidBodyComponent::WorldSpringPoints()[i] = {0.0f, 0.0f, 0.0f, 0.0f};
            g_world.springs[i].anchor = c->m_pos;
        }
    }
    if (r.OneIn(8)) {
        c->m_vel = {0.0f, 0.0f, 0.0f, 0.0f};
        c->m_rotVel = {0.0f, 0.0f, 0.0f, 0.0f};
    } else if (r.OneIn(6)) {
        c->m_vel = {0.0f, r.Value() / 10.0f, 0.0f, 0.0f};
        c->m_rotVel = {0.0f, 0.0f, 0.0f, 0.0f};
    }
    GameVariable<uint8_t>(0x006939D4) = r.OneIn(4);
    GameVariable<uint8_t>(0x006939D5) = r.OneIn(4);

    g_a = {};
    g_a.hits = r.OneIn(4) ? 0x1F : r.Next();
    g_a.contacts = r.Bit();
    g_a.voices = r.OneIn(3) ? 36 : r.Below(48);
    for (float& d : g_a.distance)
        d = r.OneIn(4) ? 0.0f : r.Unit();
    g_a.hitObject = r.OneIn(3);
    Normal(r, g_a.normal);
    Fill(r, g_a.point, 16, 1.0f / 50.0f);
    g_frameSeconds = r.OneIn(3) ? 1.0 / 60.0 : r.Value() / 50000.0;
}

// Points whose turned coordinates show the order of the sums: for one output (column `c` of `m`) two of the three
// terms cancel exactly and the third is tiny - it survives only when added last.
void CraftPoint(Random& r, const Mth::Matrix& m, float* p)
{
    int c = static_cast<int>(r.Below(3));
    int tiny = static_cast<int>(r.Below(3));
    int a = (tiny + 1) % 3, b = (tiny + 2) % 3;
    auto at = [&m, c](int row) { return reinterpret_cast<const float*>(&m.row[row])[c]; };
    p[a] = at(b);
    p[b] = -at(a);
    p[tiny] = at(tiny) != 0.0f ? 1.0f / 1099511627776.0f : 0.0f; // 2^-40
}

void CraftIntegrate(Random& r, CRigidBodyComponent* c)
{
    // With dt 0 the orientation is only normalised: its matrix is known beforehand.
    Mth::Quat q = c->m_orientation;
    Original<void*(__thiscall*)(void*, float)>(0x0042E310)(&q, 1.0f);
    Mth::Matrix m;
    Original<void(__thiscall*)(const void*, void*)>(0x0042BC40)(&q, &m);
    for (int i = 0; i < kMaxPoints; ++i) {
        CraftPoint(r, m, &g_world.points[i].x);
        CraftPoint(r, m, &g_world.spheres[i].center.x);
    }
    for (auto& spring : g_world.springs)
        CraftPoint(r, m, &spring.point.x);
}

// A not-a-number's sign and payload aren't kept alike by the x87 and SSE (which of two operands is passed on, an
// FCHS of the default one): every NaN is traced as one value.
uint32_t Word(uint32_t word) { return (word & 0x7F800000) == 0x7F800000 && (word & 0x007FFFFF) ? 0x7FC0DEADu : word; }

void Trace(std::vector<uint32_t>& trace)
{
    for (size_t i = 0; i < 0x1A0; i += 4)
        trace.push_back(Word(Rel(*reinterpret_cast<const uint32_t*>(g_world.component + i))));
    for (size_t i = 0; i < 0x120; i += 4)
        trace.push_back(Word(Rel(*reinterpret_cast<const uint32_t*>(g_world.object + i))));
    for (uint32_t i = 0; i < kWorldArraysSize; i += 4)
        trace.push_back(Word(*reinterpret_cast<const uint32_t*>(kWorldArrays + i)));
    for (size_t i = 0; i < g_heapUsed; i += 4)
        trace.push_back(Rel(*reinterpret_cast<const uint32_t*>(g_world.heap + i)));
    trace.push_back(g_heapUsed);
}

void TestRigidBodyPhysics()
{
    std::vector<Saved> saved = {{0x007CC979, 1, {}},   {0x007D1800, 4, {}},          {0x006F3614, 0x10, {}},
                                {0x006F85A8, 4, {}},   {0x006939D4, 4, {}},          {kWorldArrays, kWorldArraysSize, {}}};
    SaveAll(saved);
    GameVariable<uint8_t>(0x007CC979) = 1;
    GameVariable<uint32_t>(0x007D1800) = 0x3A3A0000; // (the stand-ins don't look at the manager)
    std::memset(&GameVariable<uint8_t>(0x006F3614), 0, 0x10); // (no struct pools: Mem::New)
    GameVariable<uint32_t>(0x006F85A8) = 0x5F5F0000;
    auto* s = new StandIns;
    StandIn(*s);
    // The object's vtable: slot 6 MarkAsDead.
    for (uint32_t& slot : g_world.vtable)
        slot = 0xDEAD0000;
    g_world.vtable[6] = reinterpret_cast<uint32_t>(s->Thunk(0, [](Regs& r) {
        Record({0xDEAD, Rel(r.ecx)});
        return 0u;
    }));
    std::vector<uint32_t> traces[2];
    int counts[8] = {};
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        Random r{0x7B0D};
        for (uint32_t n = 0; n < 6000; ++n) {
            Build(r);
            auto* c = reinterpret_cast<CRigidBodyComponent*>(g_world.component);
            uint32_t which = r.Below(10);
            Record({0xCA5E, which});
            if (original)
                ++counts[which < 8 ? which : 7];
            using Plain = void(__thiscall*)(void*);
            switch (which) {
            case 0: { // ImpulseScale
                auto* contact = &CRigidBodyComponent::Contacts()[r.Below(kMaxPoints)];
                double result = original ? Original<double(__thiscall*)(void*, const void*, const void*)>(0x0042CEE0)(
                                               c, &contact->normal, &contact->offset)
                                         : c->ImpulseScale(&contact->normal, &contact->offset);
                uint32_t words[2];
                std::memcpy(words, &result, 8);
                Record({words[0], words[1]});
                break;
            }
            case 1: // ApplySprings
                if (r.OneIn(4)) {
                    // A point moving at exactly the speed limit (0.1), or just about (its length rounds to it).
                    c->m_rotVel = {0.0f, 0.0f, 0.0f, r.Bit() ? -0.0f : 0.0f};
                    if (r.Bit()) {
                        c->m_vel = {0.1f, 0.0f, 0.0f, c->m_vel.w};
                    } else {
                        float d[4];
                        Normal(r, d);
                        c->m_vel = {d[0] * 0.1f, d[1] * 0.1f, d[2] * 0.1f, c->m_vel.w};
                    }
                    c->m_springCount = 1;
                    g_world.springs[0].active = true;
                }
                if (original)
                    Original<Plain>(0x0042CF90)(c);
                else
                    c->ApplySprings();
                break;
            case 2: // DebugDraw (nothing drawn: nothing may change)
                if (original)
                    Original<Plain>(0x0042D9D0)(c);
                else
                    c->DebugDraw();
                break;
            case 3: { // Integrate
                float dt = r.OneIn(3) ? 1.0f / 60.0f : r.Value() / 50000.0f;
                if (r.OneIn(3)) {
                    CraftIntegrate(r, c);
                    dt = 0.0f;
                } else if (r.OneIn(2)) { // (a position near the step: the step's rounding shows)
                    c->m_pos = {r.Value() / 2000.0f, r.Value() / 2000.0f, r.Value() / 2000.0f, r.Value() / 2000.0f};
                }
                if (original)
                    Original<void(__thiscall*)(void*, float)>(0x0042E540)(c, dt);
                else
                    c->Integrate(dt);
                break;
            }
            case 4: { // OnBounce
                float speed = r.OneIn(5) ? c->m_bounceCallbackThreshold : r.Value() / 10.0f;
                if (r.Bit()) { // (a sound at a part of the full speed)
                    c->mp_soundComponent = reinterpret_cast<Obj::CSoundComponent*>(g_world.sound);
                    c->mp_bounceSound = reinterpret_cast<Script::CStruct*>(g_world.structs[1]);
                    g_a.voices = r.Below(30);
                    c->m_bounceCallbackThreshold = -1.0f;
                    speed = c->m_bounceFullSpeed * r.Unit();
                }
                const auto* point = reinterpret_cast<const Mth::Vector*>(g_a.point);
                if (original)
                    Original<void(__thiscall*)(void*, float, const void*)>(0x0042E980)(c, speed, point);
                else
                    c->OnBounce(speed, point);
                break;
            }
            case 5: // ResolveContacts
                if (r.OneIn(4)) {
                    // One contact sliding at exactly the friction's limit (0.001 across the normal).
                    auto& k = CRigidBodyComponent::Contacts()[0];
                    k.offset = {0.0f, 0.0f, 0.0f, 0.0f};
                    k.normal = {0.0f, 1.0f, 0.0f, 0.0f};
                    k.active = true;
                    c->m_contactsHit = 1;
                    c->m_vel = {0.001f, r.Value() / 100.0f, 0.0f, 0.0f};
                    c->m_rotVel = {0.0f, 0.0f, 0.0f, 0.0f};
                    c->mp_directedFriction = nullptr;
                } else if (r.OneIn(3)) {
                    // Directed friction along the normals (nothing of it left across them).
                    std::memset(&c->m_matrix, 0, sizeof(c->m_matrix));
                    for (int k = 0; k < 4; ++k)
                        reinterpret_cast<float*>(&c->m_matrix.row[k])[k] = 1.0f;
                    c->mp_directedFriction = g_world.friction;
                    for (int k = 0; k < kMaxPoints; ++k) {
                        auto& contact = CRigidBodyComponent::Contacts()[k];
                        g_world.friction[contact.friction].direction = contact.normal;
                        g_world.friction[contact.friction].active = true;
                    }
                }
                if (original)
                    Original<Plain>(0x00430140)(c);
                else
                    c->ResolveContacts();
                break;
            case 6: { // Collide
                std::memset(g_world.feeler, 0, sizeof(g_world.feeler));
                std::memcpy(g_world.feeler + 0x78, g_a.point, 16);
                std::memcpy(g_world.feeler + 0x88, g_a.normal, 16);
                if (r.OneIn(5)) { // (moving along the normal only: nothing across it)
                    float k = r.Value() / 10.0f;
                    c->m_vel = {g_a.normal[0] * k, g_a.normal[1] * k, g_a.normal[2] * k, c->m_vel.w};
                } else if (r.OneIn(4)) { // (exactly the limit, 0.001, across it)
                    const float up[4] = {0.0f, 1.0f, 0.0f, 0.0f};
                    std::memcpy(g_world.feeler + 0x88, up, 16);
                    c->m_vel = {0.001f, r.Value() / 10.0f, 0.0f, 0.0f};
                }
                auto* feeler = reinterpret_cast<Nx::CFeeler*>(g_world.feeler);
                if (original)
                    Original<void(__thiscall*)(void*, void*)>(0x00430BE0)(c, feeler);
                else
                    c->Collide(feeler);
                break;
            }
            default: // Update
                if (r.OneIn(6)) // (the die timer running out exactly now)
                    c->m_dieTimer = static_cast<float>(g_frameSeconds);
                if (original)
                    Original<Plain>(0x00430E20)(c);
                else
                    c->Update();
                break;
            }
            Trace(g_trace);
        }
        traces[run] = g_trace;
    }
    delete s;
    RestoreAll(saved);
    for (size_t i = 0; i < traces[0].size() && i < traces[1].size(); ++i) {
        if (traces[0][i] != traces[1][i]) {
            size_t k = i;
            while (k > 0 && traces[0][k] != 0xCA5E)
                --k;
            Log("  first difference at %u: the case at %u (kind %u)", static_cast<unsigned>(i), static_cast<unsigned>(k),
                traces[0][k + 1]);
            break;
        }
    }
    Check(counts[7] > 500, "rigid body physics: the cases reach Update");
    Compare(traces[0], traces[1], "rigid body physics (update, integrate, collide, contacts, springs, bounce)");
}

} // namespace

SELFTEST(Early, TestRigidBodyPhysics, RemoveStandIns);
