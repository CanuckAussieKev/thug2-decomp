// Self-tests for the rigid body component's life, shapes, set-up and member functions
// (game/obj/rigidbody_component.cpp) and Mth::Quat (game/mth/quat.cpp): the originals against ours on stand-in objects,
// every call into other code recorded by stand-ins (hook/standins.h), the component, its arrays and the copies
// compared afterwards.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/matrix.h"
#include "game/mth/quat.h"
#include "game/mth/random.h"
#include "game/obj/compositemgr.h"
#include "game/obj/object.h"
#include "game/obj/objman.h"
#include "game/obj/rigidbody_component.h"
#include "game/obj/smtptr.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
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
    // A value with every bit of its mantissa used.
    float Value() { return static_cast<float>(static_cast<int>(Below(30001)) - 15000) / 7.0f; }
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

struct World {
    alignas(16) uint8_t component[0x1A0];
    alignas(16) uint8_t object[0x120];
    alignas(16) uint8_t other[0x120];
    alignas(16) uint8_t otherBody[0x1A0];
    alignas(16) uint8_t heap[0x2000];   // what the allocations get, one after another
    alignas(16) uint8_t params[0x10];
    alignas(16) uint8_t script[0x40];
    alignas(16) uint8_t scriptParams[0x10];
    alignas(16) uint8_t structs[8][0x10]; // stand-in structures (sounds, spheres, friction...)
    alignas(16) uint8_t vectors[16][0x10]; // script vectors (x at +4)
    alignas(16) uint8_t arrays[4][0x10];
    uint32_t elements[4][16];
    alignas(16) uint8_t matrix[0x40];   // a matrix for Quat::FromMatrix
    alignas(16) uint8_t quat[0x10];
    alignas(16) uint8_t component0[0x10]; // GetNextComponent's
};
World g_world;
uint32_t g_heapUsed;
bool g_debugCases = false;

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
    bool callocFails, allocFails, haveBody;
    uint32_t found, flags;
    uint32_t seed;
    float floats[8];
};
Answers g_a;

uint32_t NameBit(uint32_t name) { return 1u << ((name * 2654435761u) >> 27); }
bool Found(uint32_t name) { return (g_a.found & NameBit(name)) != 0; }

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

// Which array a name gets (contacts, spheres, springs, directed_friction).
uint32_t ArrayFor(uint32_t name)
{
    switch (name) {
    case 0xCCBFEA8C: return reinterpret_cast<uint32_t>(g_world.arrays[0]);
    case 0x5B6A8D18: return reinterpret_cast<uint32_t>(g_world.arrays[1]);
    case 0xB77BE94A: return reinterpret_cast<uint32_t>(g_world.arrays[2]);
    default: return reinterpret_cast<uint32_t>(g_world.arrays[3]);
    }
}

void StandIn(StandIns& s)
{
    using Script::CStruct;
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Record({0x1CC0, r.args[0]});
        if (g_a.callocFails)
            return 0u;
        void* p = HeapBlock(r.args[0]);
        if (p)
            std::memset(p, 0, r.args[0]);
        return reinterpret_cast<uint32_t>(p);
    });
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Record({0xAFC0, r.args[0], r.args[1], r.args[2]});
        if (g_debugCases)
            Log("  alloc %u used %u", r.args[0], g_heapUsed);
        return g_a.allocFails ? 0u : reinterpret_cast<uint32_t>(HeapBlock(r.args[0]));
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xABA0, Rel(r.args[0])});
        return 0u;
    });
    s.At({0x0045D730, AddressOf(&Obj::CBaseComponent::Construct)}, 0, [](Regs& r) {
        Record({0xD730, Rel(r.ecx)});
        reinterpret_cast<uint32_t*>(r.ecx)[0] = 0xBA5E0000;
        return r.ecx;
    });
    s.At({0x0045D740, AddressOf(&Obj::CBaseComponent::DestroyBody)}, 0, [](Regs& r) {
        Record({0xD740, Rel(r.ecx), Rel(*reinterpret_cast<uint32_t*>(r.ecx))});
        return 0u;
    });
    // Structures: copies, deletes.
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
    s.At({0x00479020, AddressOf(static_cast<CStruct* (*)(uint32_t)>(&Script::GetStructure))}, 0, [](Regs& r) {
        Record({0x9020, r.args[0]});
        return reinterpret_cast<uint32_t>(g_world.structs[0]);
    });
    // Other game code.
    s.At({0x0042BE90, AddressOf(&Obj::CRigidBodyComponent::Sleep)}, 0, [](Regs& r) {
        Record({0xBE90, Rel(r.ecx)});
        return 0u;
    });
    s.At({0x004619F0, AddressOf(&Obj::CBaseManager::GetObjectByID)}, 4, [](Regs& r) {
        Record({0x19F0, r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t>(g_world.other);
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Record({0xDB90, Rel(r.ecx), r.args[0]});
        return g_a.haveBody ? reinterpret_cast<uint32_t>(g_world.otherBody) : 0u;
    });
    s.At({0x0045C180, AddressOf(static_cast<bool (*)(Script::CStruct*, Mth::Vector*)>(SkateScript::GetAngles))}, 0,
         [](Regs& r) {
             Record({0xC180, Rel(r.args[0])});
             auto* v = reinterpret_cast<float*>(r.args[1]);
             v[0] = g_a.floats[0] / 1000.0f;
             v[1] = g_a.floats[1] / 1000.0f;
             v[2] = g_a.floats[2] / 1000.0f;
             v[3] = 1.0f;
             return 1u;
         });
    s.At({0x0041A730, AddressOf(&Mth::Matrix::InitFromAngles)}, 4, [](Regs& r) {
        const float* angles = reinterpret_cast<const float*>(r.args[0]);
        Record({0xA730, Bits(angles[0]), Bits(angles[1]), Bits(angles[2])});
        auto* m = reinterpret_cast<float*>(r.ecx);
        for (int i = 0; i < 16; ++i)
            m[i] = (i % 5 == 0 ? 1.0f : 0.0f) + angles[i % 3] * static_cast<float>(i - 7) / 9.0f;
        if (g_a.seed & 0x200) {
            // A turn that puts the crafted points' sums on a float tie (the order of the additions shows).
            const float one = 1.0f + 1.0f / 4096.0f, step = 1.0f / 67108864.0f;
            for (int c = 0; c < 3; ++c) {
                m[c] = one;
                m[4 + c] = step;
                m[8 + c] = step;
            }
        }
        return r.ecx;
    });
    s.At({0x004637C0, AddressOf(&Obj::CRefCounted::AddSmartPointer)}, 4, [](Regs& r) {
        Record({0x37C0, Rel(r.ecx), Rel(r.args[0])});
        return 0u;
    });
    s.At({0x004637E0, AddressOf(&Obj::CRefCounted::RemoveSmartPointer)}, 4, [](Regs& r) {
        Record({0x37E0, Rel(r.ecx), Rel(r.args[0])});
        return 0u;
    });
    // The script structures: a name found or not by its hash; values from it.
    auto getter = [](uint32_t tag) {
        return [tag](Regs& r) {
            uint32_t name = tag == 0x7010 ? Hash(reinterpret_cast<const char*>(r.args[0])) : r.args[0];
            Record({tag, Rel(r.ecx), name, r.args[2]});
            if (g_debugCases)
                Log("  get %04X %08X", tag, name);
            // (asserted ones, structures and arrays are always there: the original's are garbage otherwise)
            if (r.args[2] == 0 && tag != 0x67C0 && tag != 0x6860 && !Found(name ^ Rel(r.ecx)))
                return 0u;
            uint32_t* out = reinterpret_cast<uint32_t*>(r.args[1]);
            switch (tag) {
            case 0x66D0: {
                float f = g_a.floats[(name * 7u) >> 29];
                std::memcpy(out, &f, 4);
                break;
            }
            case 0x6630: *out = 3 + (g_a.seed % 7); break;
            case 0x6950: *out = name == 0x61FBB62C ? (g_a.seed & 3) == 0 ? 0x4484B712 : (g_a.seed & 3) == 1 ? 0x7B01911C
                                                                         : (g_a.seed & 3) == 2 ? 0x99A34896 : 0x1234
                                                    : name ^ g_a.seed;
                break;
            case 0x6FB0:
            case 0x7010:
                if (name == 0 && (g_a.seed & 0x200)) {
                    const float crafted[4] = {1.0f + 1.0f / 4096.0f, -1.0f / 134217728.0f,
                                              (1.0f + 1.0f / 128.0f) / 134217728.0f, 0.0f};
                    std::memcpy(out, crafted, 16);
                    break;
                }
                for (int i = 0; i < 4; ++i) {
                    float f = (g_a.seed & 0x40) && name == 0x737612C6 ? 0.0f : g_a.floats[(name + i) & 7];
                    std::memcpy(out + i, &f, 4);
                }
                break;
            case 0x6860: *out = ArrayFor(name); break;
            case 0x67C0: *out = reinterpret_cast<uint32_t>(g_world.structs[1 + (name & 3)]); break;
            }
            return 1u;
        };
    };
    s.At({0x00476950, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))}, 12,
         getter(0x6950));
    s.At({0x00476630, AddressOf(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))}, 12,
         getter(0x6630));
    s.At({0x004766D0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, float*, int)>(&CStruct::GetFloat))}, 12,
         getter(0x66D0));
    s.At({0x00476FB0,
          AddressOf(static_cast<bool (CStruct::*)(uint32_t, Script::VectorValue*, int)>(&CStruct::GetVector))},
         12, getter(0x6FB0));
    s.At({0x00477010,
          AddressOf(static_cast<bool (CStruct::*)(const char*, Script::VectorValue*, int)>(&CStruct::GetVector))},
         12, getter(0x7010));
    s.At({0x00476860, AddressOf(static_cast<bool (CStruct::*)(uint32_t, Script::CArray**, int)>(&CStruct::GetArray))},
         12, getter(0x6860));
    s.At({0x004767C0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, CStruct**, int)>(&CStruct::GetStructure))}, 12,
         getter(0x67C0));
    s.At({0x00476AF0, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsComponentNamed))}, 4,
         [](Regs& r) {
             Record({0x6AF0, Rel(r.ecx), r.args[0]});
             // (CollideMuteDelay sometimes there but not a number: its slot's value stays)
             return static_cast<uint32_t>(Found(r.args[0] ^ Rel(r.ecx)) ||
                                          (r.args[0] == 0x3818EE47 && (g_a.seed & 0x100)));
         });
    s.At({0x00476B40, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsFlag))}, 4, [](Regs& r) {
        Record({0x6B40, Rel(r.ecx), r.args[0]});
        return static_cast<uint32_t>((g_a.flags & NameBit(r.args[0] ^ 0x7777)) != 0);
    });
    s.At({0x00477F70, AddressOf(static_cast<void (CStruct::*)(uint32_t, Script::VectorValue)>(&CStruct::AddVector))},
         0x14, [](Regs& r) {
             Record({0x7F70, Rel(r.ecx), r.args[0], r.args[1], r.args[2], r.args[3], r.args[4]});
             return 0u;
         });
    s.At({0x00476490, AddressOf(&CStruct::GetNextComponent)}, 4, [](Regs& r) {
        Record({0x6490, Rel(r.ecx), r.args[0]});
        return reinterpret_cast<uint32_t>(g_world.component0);
    });
}

void Fill(Random& r, uint8_t* p, size_t size)
{
    for (size_t i = 0; i < size; i += 4) {
        float f = r.Value() / 50.0f;
        std::memcpy(p + i, &f, 4);
    }
}

void Build(Random& r)
{
    g_heapUsed = 0;
    std::memset(g_world.heap, 0xCD, sizeof(g_world.heap));
    Fill(r, g_world.component, sizeof(g_world.component));
    Fill(r, g_world.object, sizeof(g_world.object));
    Fill(r, g_world.other, sizeof(g_world.other));
    Fill(r, g_world.otherBody, sizeof(g_world.otherBody));
    auto* c = reinterpret_cast<CRigidBodyComponent*>(g_world.component);
    c->m_object = reinterpret_cast<Obj::CCompositeObject*>(g_world.object);
    // The arrays it owns: none, or blocks of the heap (freed by the stand-in, not really).
    c->mp_contacts = r.Bit() ? nullptr : reinterpret_cast<Mth::Vector*>(g_world.heap + 0x1F00);
    c->mp_directedFriction = r.Bit() ? nullptr : reinterpret_cast<CRigidBodyComponent::DirectedFriction*>(g_world.heap + 0x1F40);
    c->mp_spheres = r.Bit() ? nullptr : reinterpret_cast<CRigidBodyComponent::Sphere*>(g_world.heap + 0x1F80);
    c->mp_springs = r.Bit() ? nullptr : reinterpret_cast<CRigidBodyComponent::Spring*>(g_world.heap + 0x1FC0);
    c->m_contactCount = static_cast<uint16_t>(r.OneIn(3) || !c->mp_contacts ? 0 : r.Below(3));
    c->m_sphereCount = 0;
    c->m_springCount = 0;
    for (Script::CStruct** s : {&c->mp_callbackParams, &c->mp_collideSound, &c->mp_bounceSound})
        *s = r.Bit() ? nullptr : reinterpret_cast<Script::CStruct*>(g_world.structs[5 + r.Below(3)]);
    c->m_state = static_cast<int>(r.Below(3));
    c->m_flags = r.Next();
    // The script arrays: vectors (contacts, springs), structures (spheres, friction).
    for (int a = 0; a < 4; ++a) {
        auto* array = reinterpret_cast<Script::CArray*>(g_world.arrays[a]);
        std::memset(array, 0, sizeof(*array));
        array->m_size = 1 + r.Below(6);
        for (int k = 0; k < 16; ++k)
            g_world.elements[a][k] = (a == 0 || a == 2) ? reinterpret_cast<uint32_t>(g_world.vectors[(k + a) % 16])
                                                         : reinterpret_cast<uint32_t>(g_world.structs[1 + (k % 4)]);
        if (array->m_size == 1)
            array->m_single = g_world.elements[a][0];
        else
            array->m_elements = g_world.elements[a];
    }
    for (auto& v : g_world.vectors)
        Fill(r, v, sizeof(v));
    uint32_t edge = r.Below(8);
    for (int k = 0; k < 16; ++k) {
        float* v = reinterpret_cast<float*>(g_world.vectors[k]) + 1; // (x at +4)
        if (edge == 0) {         // crafted for the turn's tie (with the crafted matrix)
            v[0] = 1.0f + 1.0f / 4096.0f;
            v[1] = -1.0f / 134217728.0f;
            v[2] = (1.0f + 1.0f / 128.0f) / 134217728.0f;
        } else if (edge == 1) {  // +-30 along x: the size limit exactly (the average is 0 for an even count)
            v[0] = (k & 1) ? 30.0f : -30.0f;
            v[1] = v[2] = 0.0f;
        } else if (edge == 2 && k == 0) { // not a number, the last of the contacts looked at
            v[1] = std::numeric_limits<float>::quiet_NaN();
        }
    }
    for (auto& m : g_world.structs)
        std::memset(m, 0, sizeof(m));
    std::memset(g_world.component0, 0, sizeof(g_world.component0));
    At<uint32_t>(g_world.component0, 8) = 0x1D000000 + r.Below(9);
    g_a = {};
    g_a.callocFails = r.OneIn(8);
    g_a.allocFails = false; // (a failed array is written through: a crash in both)
    g_a.haveBody = r.Bit();
    g_a.found = r.Next() ^ (r.Next() << 12);
    g_a.flags = r.Next() ^ (r.Next() << 12);
    g_a.seed = r.Next() | (edge == 0 ? 0x200u : 0u);
    if (edge != 0)
        g_a.seed &= ~0x200u;
    if (edge <= 2) {
        // The crafted points as the shape: contacts= given, no shape flag, Angles (for the tie), no centre of mass and
        // no mass_over_moment (for the size limit / not a number).
        uint32_t params = Rel(reinterpret_cast<uint32_t>(g_world.params));
        g_a.found |= NameBit(0xCCBFEA8C ^ params);
        g_a.found &= ~(NameBit(0x737612C6 ^ params) | NameBit(0x967CD3AE ^ params) | NameBit(0x5B6A8D18 ^ params));
        if (edge == 0)
            g_a.found |= NameBit(0x9D2D0915 ^ params); // (Angles: the crafted turn)
        else
            g_a.found &= ~NameBit(0x9D2D0915 ^ params);
        if (edge == 2) { // (a centre of mass given: the points aren't all made not-a-number by the average)
            g_a.found |= NameBit(0x737612C6 ^ params);
            g_a.seed &= ~0x40u;
        }
        for (uint32_t flag : {0xF756B7C5u, 0x1CB1A2B6u, 0x64FBA415u, 0x20689278u})
            g_a.flags &= ~NameBit(flag ^ 0x7777);
    }
    if (r.OneIn(6)) // (a slow other object: under 1 up or down)
        At<float>(g_world.other, 0xB0) = r.Value() / 20000.0f;
    for (float& f : g_a.floats)
        f = r.OneIn(7) ? 0.0f : r.Value() / 30.0f;
    // A matrix for the quaternion: a random one, or a turn whose trace is negative (the other branch).
    Fill(r, g_world.matrix, sizeof(g_world.matrix));
    if (r.Bit()) {
        auto* m = reinterpret_cast<float*>(g_world.matrix);
        m[0] = -std::fabs(m[0]) - 1.0f;
        m[5] = r.Bit() ? m[5] : -std::fabs(m[5]);
        if (r.OneIn(3))
            m[5] = m[0]; // (a tie for the largest)
    }
    Fill(r, g_world.quat, sizeof(g_world.quat));
    if (r.OneIn(8))
        std::memset(g_world.quat, 0, sizeof(g_world.quat));
    // The random streams (PlusOrMinus).
    GameVariable<uint32_t[3]>(0x00690FD4)[0] = 0x0AA2FB3F;
    GameVariable<uint32_t[3]>(0x00690FD4)[1] = 0x12B9B0A1;
    GameVariable<uint32_t[3]>(0x00690FD4)[2] = r.Next();
    GameVariable<uint32_t>(0x006939D0) = 0;
}

void Trace(std::vector<uint32_t>& trace)
{
    for (size_t i = 0; i < 0x1A0; i += 4)
        trace.push_back(Rel(*reinterpret_cast<const uint32_t*>(g_world.component + i)));
    for (size_t i = 0; i < g_heapUsed && i < 0x1000; i += 4)
        trace.push_back(Rel(*reinterpret_cast<const uint32_t*>(g_world.heap + i)));
    trace.push_back(g_heapUsed);
    for (size_t i = 0; i < 0x10; i += 4)
        trace.push_back(*reinterpret_cast<const uint32_t*>(g_world.quat + i));
    trace.push_back(GameVariable<uint32_t>(0x006939D0));
}

void TestRigidBodySetup()
{
    std::vector<Saved> saved = {{0x007CC979, 1, {}}, {0x007D1800, 4, {}}, {0x006F3614, 0x10, {}}, {0x006F0FB8, 4, {}},
                                {0x006939D0, 4, {}}, {0x00690FD4, 12, {}}, {0x00695150, 0x60, {}}, {0x006939B0, 0x20, {}}};
    SaveAll(saved);
    GameVariable<uint8_t>(0x007CC979) = 1;
    GameVariable<uint32_t>(0x007D1800) = 0x3A3A0000; // (the stand-ins don't look at the manager)
    std::memset(&GameVariable<uint8_t>(0x006F3614), 0, 0x10); // (no struct pools: Mem::New)
    GameVariable<uint32_t>(0x006F0FB8) = 0xC0B70000;
    auto* s = new StandIns;
    StandIn(*s);
    std::vector<uint32_t> traces[2];
    int inits = 0;
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        Random r{0x4B0D};
        for (uint32_t n = 0; n < 4000; ++n) {
            Build(r);
            auto* c = reinterpret_cast<CRigidBodyComponent*>(g_world.component);
            auto* params = reinterpret_cast<Script::CStruct*>(g_world.params);
            auto* script = reinterpret_cast<Script::CScript*>(g_world.script);
            At<uint8_t*>(g_world.script, 0x14) = g_world.scriptParams;
            uint32_t which = r.Below(17);
            Record({0xCA5E, which});
            if (g_debugCases) Log("rb case %u run %d kind %u", n, run, which);
            using Member = bool(__thiscall*)(void*, void*, void*);
            switch (which) {
            case 0: { // Create / Construct, Destroy
                void* made = original ? Original<void*(__cdecl*)()>(0x0042EF00)() : CRigidBodyComponent::Create();
                Record({Rel(reinterpret_cast<uint32_t>(made))});
                if (made)
                    for (size_t i = 0; i < 0x1A0; i += 4)
                        Record({Rel(*reinterpret_cast<const uint32_t*>(static_cast<uint8_t*>(made) + i))});
                unsigned flags = r.Below(4);
                void* result = original ? Original<void*(__thiscall*)(void*, unsigned)>(0x00430DF0)(c, flags) : c->Destroy(flags);
                Record({Rel(reinterpret_cast<uint32_t>(result))});
                break;
            }
            case 1:
            case 2:
            case 3:
            case 4: // InitFromStructure
                if (original)
                    Original<void(__thiscall*)(void*, void*)>(0x0042F050)(c, params);
                else
                    c->InitFromStructure(params);
                break;
            case 5: { // the shapes directly
                Mth::Vector top = {r.Value(), r.Value(), r.Value(), 1.0f}, bottom = {r.Value(), r.Value(), r.Value(), 1.0f};
                float a = r.Value(), b = r.Value(), h = r.Value();
                int edges = static_cast<int>(1 + r.Below(9));
                auto* points = reinterpret_cast<Script::CArray*>(g_world.arrays[0]);
                auto* spheres = reinterpret_cast<Script::CArray*>(g_world.arrays[1]);
                g_a.found |= NameBit(0u ^ Rel(reinterpret_cast<uint32_t>(g_world.structs[1]))); // (asserted)
                if (original) {
                    Original<void(__thiscall*)(void*, void*)>(0x0042D4B0)(c, points);
                    Original<void(__thiscall*)(void*, void*, void*)>(0x0042D560)(c, &top, &bottom);
                    Original<void(__thiscall*)(void*, float, float)>(0x0042D690)(c, a, b);
                    Original<void(__thiscall*)(void*, float, float, float, int)>(0x0042D790)(c, a, b, h, edges);
                    Original<void(__thiscall*)(void*, void*)>(0x0042D8A0)(c, &top);
                    Original<void(__thiscall*)(void*, void*)>(0x0042ED60)(c, spheres);
                } else {
                    c->SetContacts(points);
                    c->SetBox(&top, &bottom);
                    c->SetPyramid(a, b);
                    c->SetCylinder(a, b, h, edges);
                    c->SetTriangle(&top);
                    c->SetSpheres(spheres);
                }
                break;
            }
            case 6: { // SetSounds
                uint32_t type = 0x50000000 + r.Below(4);
                g_a.found |= NameBit(type ^ Rel(reinterpret_cast<uint32_t>(g_world.structs[0]))); // (no type: a crash)
                if (original)
                    Original<void(__thiscall*)(void*, uint32_t)>(0x0042EB10)(c, type);
                else
                    c->SetSounds(type);
                break;
            }
            case 7:
            case 8: { // Quat
                auto* q = reinterpret_cast<Mth::Quat*>(g_world.quat);
                const auto* m = reinterpret_cast<const Mth::Matrix*>(g_world.matrix);
                float length = r.OneIn(3) ? 1.0f : r.Value();
                void* a = original ? Original<void*(__thiscall*)(void*, const void*)>(0x0042E1F0)(q, m) : q->FromMatrix(*m);
                Record({Rel(reinterpret_cast<uint32_t>(a))});
                for (size_t i = 0; i < 16; i += 4)
                    Record({*reinterpret_cast<const uint32_t*>(g_world.quat + i)});
                if (r.OneIn(4))
                    std::memset(g_world.quat, 0, sizeof(g_world.quat));
                void* b = original ? Original<void*(__thiscall*)(void*, float)>(0x0042E310)(q, length) : q->Normalize(length);
                Record({Rel(reinterpret_cast<uint32_t>(b))});
                break;
            }
            case 9: { // AddRigidBodyPusher
                auto* slots = reinterpret_cast<uint32_t*>(0x00695150);
                for (int k = 0; k < 24; k += 3)
                    slots[k] = r.OneIn(3) ? reinterpret_cast<uint32_t>(g_world.other) : 0u;
                void* object = r.OneIn(5) ? nullptr : g_world.object;
                if (original)
                    Original<void(__cdecl*)(void*)>(0x0042E380)(object);
                else
                    Obj::AddRigidBodyPusher(static_cast<Obj::CCompositeObject*>(object));
                for (int k = 0; k < 24; ++k)
                    Record({Rel(slots[k])});
                for (int k = 0; k < 8; ++k)
                    Record({GameVariable<uint32_t[8]>(0x006939B0)[k]});
                break;
            }
            default: { // the member functions
                static const uint32_t kOriginals[] = {0x0042DCA0, 0x0042DCF0, 0x0042DD00, 0x0042DEB0, 0x0042EE30,
                                                      0x0042DFB0, 0x0042DFC0};
                int k = static_cast<int>(which - 10);
                bool pick = r.Bit();
                bool result;
                if (original) {
                    uint32_t address = k < 5 ? kOriginals[k] : k == 5 ? (pick ? 0x0042DFB0 : 0x0042DFC0)
                                                              : (pick ? 0x0042DFD0 : 0x0042E020);
                    result = Original<Member>(address)(c, params, script);
                } else {
                    switch (k) {
                    case 0: result = c->RigidBody_Wake(params, script); break;
                    case 1: result = c->RigidBody_Sleep(params, script); break;
                    case 2: result = c->Rigidbody_Kick(params, script); break;
                    case 3: result = c->RigidBody_SetVelocities(params, script); break;
                    case 4: result = c->RigidBody_Enable(params, script); break;
                    case 5: result = pick ? c->RigidBody_DisablePlayerCollision(params, script)
                                             : c->RigidBody_EnablePlayerCollision(params, script);
                        break;
                    default: result = pick ? c->GetCenterOfMassPos(params, script)
                                              : c->Rigidbody_MatchVelocityTo(params, script);
                        break;
                    }
                }
                Record({result});
                break;
            }
            }
            if (original)
                inits += which >= 1 && which <= 4;
            Trace(g_trace);
            for (size_t i = 0; i < 0x120; i += 4)
                g_trace.push_back(Rel(*reinterpret_cast<const uint32_t*>(g_world.object + i)));
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
    Check(inits > 500, "rigid body set-up: the cases reach InitFromStructure");
    Compare(traces[0], traces[1], "rigid body set-up (create, shapes, init, sounds, quaternion, member functions)");
}

} // namespace

SELFTEST(Early, TestRigidBodySetup, RemoveStandIns);
