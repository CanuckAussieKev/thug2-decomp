// Self-tests for the trigger component's other functions (game/obj/trigger_component.cpp: Create, the destructors,
// InitFromStructure, Update, TriggerType, CheckFeeler, TriggerHits, FeelerHit), the display matrix component, the
// static vehicle component and the vehicle's wheel settings: the originals against ours on stand-in objects, every
// call they make into other code recorded by stand-ins (hook/standins.h) and the objects compared afterwards.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/nx/feeler.h"
#include "game/obj/display_matrix_component.h"
#include "game/obj/object.h"
#include "game/obj/static_vehicle.h"
#include "game/obj/trigger_component.h"
#include "game/obj/vehicle_wheel.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/gfx/model.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using Obj::CTriggerComponent;

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
    float Range(float low, float high) { return low + (high - low) * static_cast<float>(Below(10001)) / 10000.0f; }
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

// The stand-in world.
struct World {
    alignas(16) uint8_t component[0x80];
    alignas(16) uint8_t object[0x120];
    alignas(16) uint8_t other[0x120];      // an object hit / being tested
    alignas(16) uint8_t nodeArray[0x40];   // its nodearray component
    alignas(16) uint8_t block[0x80];       // what Calloc gives
    alignas(16) uint8_t feeler[0xB0];
    alignas(16) uint8_t hits[0xD00];
    alignas(16) uint8_t arrays[8][0xC];
    uint32_t elements[8][8];
    alignas(16) uint8_t params[0x10];
    alignas(16) uint8_t model[0x240];       // the model component (+0x208 the model)
    alignas(16) uint8_t hierarchy[0x240];
    alignas(16) uint8_t skeletonComponent[0x20];
    alignas(16) uint8_t skeleton[0x10];
    alignas(16) uint8_t bones[8][0x40];
    alignas(16) uint8_t wheel[0x100];
    void* nodeArrayVtable[16];
};
World g_world;

uint32_t Rel(uint32_t word)
{
    auto base = reinterpret_cast<uint32_t>(&g_world);
    if (word >= base && word < base + sizeof(World))
        return 0x77000000 + (word - base);
    if (word == reinterpret_cast<uint32_t>(Original<void*>(0x00434D70)) ||
        word == AddressOf(&CTriggerComponent::FeelerHit) ||
        word == reinterpret_cast<uint32_t>(&CTriggerComponent::FeelerHit)) // (through the incremental linker's jump)
        return 0xFEE1C0DE; // the feeler's callback (the original's or ours)
    return word;
}

// What the stand-ins answer this case.
struct Answers {
    uint32_t testedOwner;
    bool alive;
    bool hitObject;
    bool hasNodeArray;
    bool collision;
    bool callocFails;
    bool triggerResult;
    bool smartPointer;
    int boneCount;
    uint32_t contains;     // a bit for each name asked about (by its hash)
    uint32_t checksum;
    uint32_t driveChecksum;
    bool checksumFound;
    float floats[8];
};
Answers g_a;

uint32_t NameBit(uint32_t name) { return 1u << ((name * 2654435761u) >> 27); }

void StandIn(StandIns& s)
{
    using Script::CStruct;
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Record({0x1CC0, r.args[0]});
        if (g_a.callocFails)
            return 0u;
        std::memset(g_world.block, 0, sizeof(g_world.block));
        return reinterpret_cast<uint32_t>(g_world.block);
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
        Record({0xABA0, Rel(r.args[0])});
        return 0u;
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Record({0xDB90, Rel(r.ecx), r.args[0]});
        switch (r.args[0]) {
        case 0xC472ECC5: return g_a.hasNodeArray ? reinterpret_cast<uint32_t>(g_world.nodeArray) : 0u;
        case 0x286A8D26: return reinterpret_cast<uint32_t>(g_world.model);
        case 0x222756D5: return reinterpret_cast<uint32_t>(g_world.skeletonComponent);
        default: return 0u;
        }
    });
    g_world.nodeArrayVtable[13] = s.Thunk(0, [](Regs& r) {
        Record({0x0DE5, Rel(r.ecx)});
        return 0x0A770000u;
    });
    // The feeler.
    s.At({0x004172F0, AddressOf(&Nx::CFeeler::GetCollision)}, 8, [](Regs& r) {
        auto* f = reinterpret_cast<Nx::CFeeler*>(r.ecx);
        Record({0x72F0, r.args[0] & 0xFF, r.args[1] & 0xFF, Bits(f->m_line.start.x), Bits(f->m_line.start.y),
                Bits(f->m_line.start.z), Bits(f->m_line.start.w), Bits(f->m_line.end.x), Bits(f->m_line.end.y),
                Bits(f->m_line.end.z), Bits(f->m_line.end.w), Rel(reinterpret_cast<uint32_t>(f->m_callback)),
                Rel(f->m_unknownA8), f->m_hit.ignoreFlags, f->m_hit.requireFlags});
        if (g_a.smartPointer)
            f->m_object.mp_object = reinterpret_cast<Obj::CCompositeObject*>(g_world.other);
        return static_cast<uint32_t>(g_a.collision);
    });
    s.At({0x004637E0, AddressOf(&Obj::CRefCounted::RemoveSmartPointer)}, 4, [](Regs& r) {
        Record({0x37E0, Rel(r.ecx)}); // (the pointer itself is on the stack)
        return 0u;
    });
    s.At({0x00417140, AddressOf(&Nx::CFeeler::GetTestedOwner)}, 0, [](Regs& r) {
        Record({0x7140, Rel(r.ecx)});
        return g_a.testedOwner;
    });
    s.At({0x00417210, AddressOf(&Nx::CFeeler::HitObjectAlive)}, 0, [](Regs& r) {
        Record({0x7210, Rel(r.ecx)});
        if (!g_a.alive)
            reinterpret_cast<Nx::CFeeler*>(r.ecx)->m_hit.objectChecksum = 0;
        return static_cast<uint32_t>(g_a.alive);
    });
    s.At({0x00417240, AddressOf(&Nx::CFeeler::GetHitObject)}, 0, [](Regs& r) {
        Record({0x7240, Rel(r.ecx)});
        return g_a.hitObject ? reinterpret_cast<uint32_t>(g_world.other) : 0u;
    });
    s.At({0x00434A60, AddressOf(&CTriggerComponent::TriggerNode)}, 16, [](Regs& r) {
        const int* events = reinterpret_cast<const int*>(r.args[0]);
        Record({0x4A60, Rel(r.ecx), static_cast<uint32_t>(events[0]), static_cast<uint32_t>(events[1]),
                static_cast<uint32_t>(events[2]), r.args[1], r.args[2], Rel(r.args[3])});
        return static_cast<uint32_t>(g_a.triggerResult);
    });
    // The script structures.
    s.At({0x00476860, AddressOf(static_cast<bool (CStruct::*)(uint32_t, Script::CArray**, int)>(&CStruct::GetArray))},
         12, [](Regs& r) {
             Record({0x6860, Rel(r.ecx), r.args[0], *reinterpret_cast<uint32_t*>(r.args[1]), r.args[2]});
             *reinterpret_cast<uint32_t*>(r.args[1]) = reinterpret_cast<uint32_t>(g_world.arrays[0]);
             return 1u;
         });
    s.At({0x00476AF0, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsComponentNamed))}, 4,
         [](Regs& r) {
             Record({0x6AF0, Rel(r.ecx), r.args[0]});
             return static_cast<uint32_t>((g_a.contains & NameBit(r.args[0])) != 0);
         });
    s.At({0x00476950, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))}, 12,
         [](Regs& r) {
             auto* out = reinterpret_cast<uint32_t*>(r.args[1]);
             Record({0x6950, Rel(r.ecx), r.args[0], Rel(*out), r.args[2]});
             if (!g_a.checksumFound)
                 return 0u;
             *out = r.args[0] == 0x97E20A70 ? g_a.driveChecksum : g_a.checksum;
             return 1u;
         });
    s.At({0x004766D0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, float*, int)>(&CStruct::GetFloat))}, 12,
         [](Regs& r) {
             Record({0x66D0, Rel(r.ecx), r.args[0], Rel(r.args[1]), r.args[2]});
             if (!(g_a.contains & NameBit(r.args[0])))
                 return 0u;
             *reinterpret_cast<float*>(r.args[1]) = g_a.floats[(r.args[0] * 7u) >> 29];
             return 1u;
         });
    s.At({0x0046F060, AddressOf(&Script::RegisterMemberFunctionTable)}, 0, [](Regs& r) {
        Record({0xF060, r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    // The static vehicle's model hierarchy and skeleton.
    s.At({0x00493040, AddressOf(&Nx::CModel::GetAssetHierarchy)}, 0, [](Regs& r) {
        Record({0x3040, Rel(r.ecx)});
        return reinterpret_cast<uint32_t>(g_world.hierarchy);
    });
    s.At({0x004A2BA0}, 0, [](Regs& r) {
        Record({0x2BA0, Rel(r.ecx)});
        return static_cast<uint32_t>(g_a.boneCount);
    });
}

void Fill(Random& r, uint8_t* p, size_t size)
{
    for (size_t i = 0; i < size; i += 4) {
        float f = static_cast<float>(static_cast<int>(r.Below(4001)) - 2000) / 8.0f;
        std::memcpy(p + i, &f, 4);
    }
}

void Build(Random& r, uint32_t seed)
{
    Fill(r, g_world.component, sizeof(g_world.component));
    Fill(r, g_world.object, sizeof(g_world.object));
    Fill(r, g_world.other, sizeof(g_world.other));
    Fill(r, g_world.hierarchy, sizeof(g_world.hierarchy));
    Fill(r, g_world.wheel, sizeof(g_world.wheel));
    for (auto& bone : g_world.bones)
        Fill(r, bone, sizeof(bone));
    std::memset(g_world.nodeArray, 0, sizeof(g_world.nodeArray));
    std::memset(g_world.block, 0xB1, sizeof(g_world.block));
    At<void**>(g_world.nodeArray, 0) = g_world.nodeArrayVtable;
    At<uint8_t*>(g_world.model, 0x208) = g_world.hierarchy; // (the model: only passed on)
    At<uint8_t*>(g_world.skeletonComponent, 0x18) = g_world.skeleton;
    At<uint8_t*>(g_world.skeleton, 0x04) = g_world.bones[0];
    auto* c = reinterpret_cast<CTriggerComponent*>(g_world.component);
    c->m_object = reinterpret_cast<Obj::CCompositeObject*>(g_world.object);
    c->m_events = r.Next() ^ (r.Next() << 16);
    if (r.OneIn(3))
        c->m_lastPos = c->m_object->m_pos;
    else if (r.OneIn(3)) {
        c->m_lastPos = c->m_object->m_pos; // (only w differs)
        c->m_lastPos.w = c->m_object->m_pos.w + 1.0f;
    }
    if (r.OneIn(8))
        c->m_lastPos.x = c->m_object->m_pos.x = std::numeric_limits<float>::quiet_NaN();
    At<uint32_t>(g_world.object, 0x104) = r.OneIn(3) ? 0x8u : r.Next() & ~0x8u;
    g_a = {};
    g_a.testedOwner = r.OneIn(3) ? reinterpret_cast<uint32_t>(g_world.other) : 0u;
    g_a.alive = r.Bit();
    g_a.hitObject = !r.OneIn(4);
    g_a.hasNodeArray = r.Bit();
    g_a.collision = r.Bit();
    g_a.callocFails = r.OneIn(5);
    g_a.triggerResult = r.Bit();
    g_a.smartPointer = r.Bit();
    g_a.boneCount = static_cast<int>(r.Below(7));
    g_a.contains = r.Next() ^ (r.Next() << 12);
    static const uint32_t kChoices[] = {0x4B358AEB, 0x613631CD, 0x85981897, 0x8A18CA56, 0x9855D7E0, 0x12345678};
    g_a.checksum = kChoices[r.Below(6)];
    g_a.driveChecksum = kChoices[r.Below(6)];
    g_a.checksumFound = !r.OneIn(4);
    for (float& f : g_a.floats)
        f = r.OneIn(6) ? 0.0f : r.Range(-50.0f, 300.0f);
    // The feeler: a trigger face or not, a name or none.
    std::memset(g_world.feeler, 0, sizeof(g_world.feeler));
    auto* f = reinterpret_cast<Nx::CFeeler*>(g_world.feeler);
    f->m_hit.faceFlag40 = r.OneIn(4) ? 0 : 0x40;
    f->m_hit.objectChecksum = r.OneIn(5) ? 0 : 0x0B000000 + seed;
    f->m_unknownA8 = reinterpret_cast<uint32_t>(g_world.component);
    // The hits: names that repeat.
    std::memset(g_world.hits, 0, sizeof(g_world.hits));
    int count = static_cast<int>(r.Below(30));
    At<int>(g_world.hits, 0xC30) = count;
    for (int i = 0; i < count; ++i) {
        At<uint32_t>(g_world.hits, 0x88 + 0x60 * i) = 0x4A000000 + r.Below(6);
        At<uint8_t>(g_world.hits, 0x8C + 0x60 * i) = r.OneIn(3) ? 0 : static_cast<uint8_t>(1 + r.Below(200));
    }
    // TriggerType's arrays: one integer array, an array of integer arrays, or of something else.
    for (int a = 0; a < 8; ++a) {
        auto* array = reinterpret_cast<Script::CArray*>(g_world.arrays[a]);
        std::memset(array, 0, sizeof(*array));
        array->m_type = r.OneIn(8) ? Script::ESYMBOLTYPE_FLOAT : Script::ESYMBOLTYPE_INTEGER;
        array->m_size = r.Below(5);
        for (uint32_t k = 0; k < 8; ++k)
            g_world.elements[a][k] = r.OneIn(5) ? static_cast<uint32_t>(-1 - static_cast<int>(r.Below(3)))
                                                : (r.OneIn(3) ? 0u : r.Below(40));
        if (array->m_size == 1)
            array->m_single = g_world.elements[a][0];
        else
            array->m_elements = g_world.elements[a];
    }
    if (r.Bit()) {
        auto* outer = reinterpret_cast<Script::CArray*>(g_world.arrays[0]);
        outer->m_type = Script::ESYMBOLTYPE_ARRAY;
        outer->m_size = 1 + r.Below(4);
        for (uint32_t k = 0; k < 8; ++k)
            g_world.elements[0][k] = reinterpret_cast<uint32_t>(g_world.arrays[1 + (k + seed) % 7]);
        if (outer->m_size == 1)
            outer->m_single = g_world.elements[0][0];
        else
            outer->m_elements = g_world.elements[0];
    }
    // (the wanted events often all set)
    if (r.Bit())
        c->m_events |= 0xFFFFFFF0u;
}

void Trace(std::vector<uint32_t>& trace)
{
    for (const uint8_t* area : {g_world.component, g_world.block, g_world.feeler, g_world.wheel, g_world.wheel + 0x80})
        for (size_t i = 0; i < 0x80; i += 4)
            trace.push_back(Rel(*reinterpret_cast<const uint32_t*>(area + i)));
    for (auto& bone : g_world.bones)
        for (size_t i = 0; i < sizeof(bone); i += 4)
            trace.push_back(*reinterpret_cast<const uint32_t*>(bone + i));
    trace.push_back(Rel(*reinterpret_cast<const uint32_t*>(g_world.object + 0x4C)));
}

void TestTriggerComponent()
{
    auto* s = new StandIns;
    StandIn(*s);
    std::vector<uint32_t> traces[2];
    int triggered = 0, felt = 0;
    static const int kEvents[3] = {3, 0, 7};
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        Random r{0x7216};
        for (uint32_t n = 0; n < 3000; ++n) {
            Build(r, n);
            auto* c = reinterpret_cast<CTriggerComponent*>(g_world.component);
            auto* feeler = reinterpret_cast<Nx::CFeeler*>(g_world.feeler);
            uint32_t which = r.Below(12);
            Record({0xCA5E, which});
            size_t before = g_trace.size();
            switch (which) {
            case 0: { // Create, DestroyBody, Destroy
                void* made = original ? Original<void*(__cdecl*)()>(0x00434B90)() : CTriggerComponent::Create();
                Record({Rel(reinterpret_cast<uint32_t>(made))});
                unsigned flags = r.Below(4);
                void* result = original ? Original<void*(__thiscall*)(void*, unsigned)>(0x00434910)(c, flags)
                                        : c->Destroy(flags);
                Record({Rel(reinterpret_cast<uint32_t>(result))});
                if (original)
                    Original<void(__thiscall*)(void*)>(0x00434900)(c);
                else
                    c->DestroyBody();
                break;
            }
            case 1: // InitFromStructure
                if (original)
                    Original<void(__thiscall*)(void*, void*)>(0x00434940)(c, g_world.params);
                else
                    c->InitFromStructure(reinterpret_cast<Script::CStruct*>(g_world.params));
                break;
            case 2:
            case 3: // Update
                if (original)
                    Original<void(__thiscall*)(void*)>(0x00434D90)(c);
                else
                    c->Update();
                break;
            case 4: { // TriggerType (and other names)
                uint32_t name = r.OneIn(5) ? 0x12345678u : 0x3A717382u;
                int result = original ? Original<int(__thiscall*)(void*, uint32_t, void*, void*)>(0x00434970)(
                                            c, name, g_world.params, nullptr)
                                      : c->CallMemberFunction(name, reinterpret_cast<Script::CStruct*>(g_world.params),
                                                              nullptr);
                Record({static_cast<uint32_t>(result)});
                break;
            }
            case 5:
            case 6: { // CheckFeeler
                bool result = original ? Original<bool(__thiscall*)(void*, const int*, void*)>(0x00434BF0)(c, kEvents, feeler)
                                       : c->CheckFeeler(kEvents, feeler);
                Record({static_cast<uint32_t>(result)});
                break;
            }
            case 7:
            case 8: // TriggerHits
                if (original)
                    Original<void(__thiscall*)(void*, const int*, const uint8_t*)>(0x00434CE0)(c, kEvents, g_world.hits);
                else
                    c->TriggerHits(kEvents, g_world.hits);
                break;
            case 9: { // FeelerHit
                bool result = original ? Original<bool(__cdecl*)(void*)>(0x00434D70)(feeler)
                                       : CTriggerComponent::FeelerHit(feeler);
                Record({static_cast<uint32_t>(result)});
                break;
            }
            case 10: { // the display matrix component
                auto* d = reinterpret_cast<Obj::CDisplayMatrixComponent*>(g_world.component);
                void* made = original ? Original<void*(__cdecl*)()>(0x004312A0)() : Obj::CDisplayMatrixComponent::Create();
                Record({Rel(reinterpret_cast<uint32_t>(made))});
                if (original)
                    Original<void(__thiscall*)(void*)>(0x00431280)(d);
                else
                    d->Update();
                for (size_t i = 0; i < 0x40; i += 4)
                    Record({At<uint32_t>(g_world.object, 0xC0 + static_cast<uint32_t>(i))});
                unsigned flags = r.Below(4);
                void* result = original ? Original<void*(__thiscall*)(void*, unsigned)>(0x00431250)(d, flags)
                                        : d->Destroy(flags);
                Record({Rel(reinterpret_cast<uint32_t>(result))});
                break;
            }
            default: { // the static vehicle component and the wheel settings
                auto* v = reinterpret_cast<Obj::CStaticVehicleComponent*>(g_world.component);
                void* made = original ? Original<void*(__cdecl*)()>(0x00433690)() : Obj::CStaticVehicleComponent::Create();
                Record({Rel(reinterpret_cast<uint32_t>(made))});
                if (original)
                    Original<void(__thiscall*)(void*)>(0x004336F0)(v);
                else
                    v->Finalize();
                unsigned flags = r.Below(4);
                void* result = original ? Original<void*(__thiscall*)(void*, unsigned)>(0x00433660)(v, flags)
                                        : v->Destroy(flags);
                Record({Rel(reinterpret_cast<uint32_t>(result))});
                if (original) {
                    Original<void(__stdcall*)(void*, void*)>(0x00434F20)(g_world.wheel, g_world.params);
                    Original<void(__cdecl*)()>(0x00434F00)();
                } else {
                    Obj::ReadWheelSettings(g_world.wheel, reinterpret_cast<Script::CStruct*>(g_world.params));
                    Obj::RegisterVehicleMemberFunctions();
                }
                break;
            }
            }
            if (original)
                for (size_t k = before; k < g_trace.size(); ++k) {
                    triggered += g_trace[k] == 0x4A60;
                    felt += g_trace[k] == 0x72F0;
                }
            Trace(g_trace);
        }
        traces[run] = g_trace;
    }
    delete s;
    Log("  trigger component: %d triggers, %d feelers in the original's run", triggered, felt);
    Check(triggered > 300 && felt > 100, "trigger component: the cases reach the triggers and the feeler");
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
    Compare(traces[0], traces[1],
            "trigger component (create, update, TriggerType, feeler, hits), display matrix, static vehicle, wheel");
}

} // namespace

SELFTEST(Early, TestTriggerComponent, RemoveStandIns);
