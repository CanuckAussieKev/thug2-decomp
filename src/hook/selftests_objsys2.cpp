// Self-tests for the objsys agent's batch OS-2: the proximity nodes and restart node lists
// (src/game/obj/proxim.cpp) and the preferences (src/game/script/preferences.cpp). The original and ours in a
// stand-in world: the node parameters, skaters, objects, sectors, scripts and memory they reach are recording
// stand-ins; the box / matrix / vector maths and the arrays are the real code.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <vector>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/geometry.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/compositemgr.h"
#include "game/obj/objman.h"
#include "game/obj/object.h"
#include "game/obj/proxim.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "game/gfx/engine.h"
#include "game/gfx/sector.h"
#include "hook/appmain_testkit.h"
#include "hook/objsys_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace AppMainTests;

template <typename F>
F Fn(bool original, uint32_t address)
{
    return reinterpret_cast<F>(Code(original, address));
}

uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

// The stand-in world, in one arena (relocated in the trace).
constexpr uint32_t kArenaSize = 0x10000;
// (allocated, not static: a static this size moves other tests' statics into segments their arenas' relocation
// takes for their own)
uint8_t* const g_arena =
    static_cast<uint8_t*>(VirtualAlloc(nullptr, kArenaSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
uint32_t g_bump;
uint32_t Base() { return reinterpret_cast<uint32_t>(g_arena); }
uint8_t* At(uint32_t offset) { return g_arena + offset; }
uint32_t Reloc(uint32_t word)
{
    if (word - Base() < kArenaSize)
        return 0xA0000000u + (word - Base());
    return OnStack(word) ? 0x57AC0000u : word;
}
uint32_t RelocPointer(const void* p) { return Reloc(reinterpret_cast<uint32_t>(p)); }

// Fake script structures (node parameters): by address, their components and flags.
struct FakeStruct {
    std::map<uint32_t, uint32_t> values;
    std::set<uint32_t> flags;
    Mth::Vector pos{};
    bool hasPos = false;
};
std::map<uint32_t, FakeStruct> g_structs; // by address
std::map<uint32_t, uint32_t> g_nodes;      // node name -> structure address
std::vector<uint32_t> g_walk;              // the node walk's structures
size_t g_walkAt;
std::map<uint32_t, uint32_t> g_byId;       // object id -> object
Lcg g_answers(1);

constexpr uint32_t kVtable = 0x0000;       // the fake objects' vtable (slots 0-9)
constexpr uint32_t kSkaters = 0x0100;      // 8 skaters, 0x100 apart
constexpr uint32_t kSectors = 0x0A00;      // 4 sectors, 0x100 apart: +0x18 collision, +0x20 box, +0x40 matrix
constexpr uint32_t kCollisionVtable = 0x0E00;
constexpr uint32_t kStructs = 0x1000;      // fake structures, 0x40 apart
constexpr uint32_t kArrays = 0x2000;       // fake arrays, 0x40 apart (elements after the array)
constexpr uint32_t kHeap = 0x4000;         // Calloc

void RecordFlags()
{
    auto* manager = GameVariable<uint8_t*>(0x006F2268);
    Record({0xF1A90000u | (manager ? manager[4] | manager[5] << 8 : 0xFFFF)});
}

void StandInProximWorld(StandIns& s)
{
    // the objects
    s.At({0x004619F0, AddressOf(&Obj::CBaseManager::GetObjectByID)}, 4, [](Regs& r) {
        auto it = g_byId.find(r.args[0]);
        uint32_t object = it == g_byId.end() ? 0 : it->second;
        Record({0x0B100000, r.args[0], Reloc(object)});
        return object;
    });
    s.At({0x0045F330, AddressOf(&Obj::CCompositeObjectManager::CreateCompositeObject)}, 0, [](Regs& r) {
        uint32_t object = Base() + g_bump;
        g_bump += 0x110;
        std::memset(reinterpret_cast<void*>(object), 0, 0x108);
        *reinterpret_cast<uint32_t*>(object) = Base() + kVtable;
        Record({0xC0C00000, Reloc(r.ecx), Reloc(object)});
        return object;
    });
    s.At({0x00461930, AddressOf(&Obj::CObject::SetID)}, 4, [](Regs& r) {
        *reinterpret_cast<uint32_t*>(r.ecx + 0x14) = r.args[0];
        g_byId[r.args[0]] = r.ecx;
        Record({0x5E710000, Reloc(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x00461480, AddressOf(&Obj::CObject::SpawnAndRunScript)}, 0x14, [](Regs& r) {
        Record({0x5A970000, Reloc(r.ecx), r.args[0], r.args[1], r.args[2] & 0xFF, r.args[3] & 0xFF,
                Reloc(r.args[4])});
        RecordFlags();
        return 0u;
    });
    s.At({0x00475790, AddressOf(static_cast<void (*)(uint32_t, Script::CStruct*, Obj::CObject*, bool, const char*)>(
                          &Script::RunScript))},
         0, [](Regs& r) {
             Record({0x52700000, r.args[0], Reloc(r.args[1]), Reloc(r.args[2]), r.args[3] & 0xFF, r.args[4]});
             RecordFlags();
             return 0u;
         });
    s.At({0x00461350, AddressOf(&Obj::CObject::SwitchScript)}, 8, [](Regs& r) {
        Record({0x5C110000, Reloc(r.ecx), r.args[0], Reloc(r.args[1])});
        return 0u;
    });
    s.At({0x0045DA90, AddressOf(&Obj::CCompositeObject::CreateComponentsFromArray)}, 8, [](Regs& r) {
        Record({0xCCFA0000, Reloc(r.ecx), Reloc(r.args[0]), Reloc(r.args[1])});
        return 0u;
    });
    // script structures
    s.At({0x00479090, AddressOf(static_cast<Script::CArray* (*)(const char*)>(&Script::GetArray))}, 0,
         [](Regs& r) {
             Record({0x6A770000, Hash(reinterpret_cast<const char*>(r.args[0]))});
             return 0x0A77A000u;
         });
    s.At({0x00476410, AddressOf(&Script::CStruct::Init)}, 0, [](Regs& r) {
        Record({0x1A170000, Reloc(r.ecx)});
        return r.ecx;
    });
    s.At({0x00477130, AddressOf(&Script::CStruct::Clear)}, 0, [](Regs& r) {
        Record({0xC1EA0000, Reloc(r.ecx)});
        return 0u;
    });
    s.At({0x00477D40, AddressOf(static_cast<void (Script::CStruct::*)(uint32_t, uint32_t)>(
                          &Script::CStruct::AddChecksum))},
         8, [](Regs& r) {
             Record({0xADDC0000, Reloc(r.ecx), r.args[0], r.args[1]});
             return 0u;
         });
    auto get = [](Regs& r) {
        FakeStruct& f = g_structs[r.ecx];
        auto it = f.values.find(r.args[0]);
        Record({0x6E7C0000, Reloc(r.ecx), r.args[0], r.args[2], it != f.values.end()});
        if (it == f.values.end())
            return 0u;
        *reinterpret_cast<uint32_t*>(r.args[1]) = it->second;
        return 1u;
    };
    s.At({0x00476950, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, uint32_t*, int)>(
                          &Script::CStruct::GetChecksum))},
         12, get);
    s.At({0x004766D0, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, float*, int)>(
                          &Script::CStruct::GetFloat))},
         12, get);
    s.At({0x004768B0, AddressOf(static_cast<bool (Script::CStruct::*)(const char*, Script::CArray**, int)>(
                          &Script::CStruct::GetArray))},
         12, [](Regs& r) {
             FakeStruct& f = g_structs[r.ecx];
             uint32_t name = Crc::GenerateCRCFromString(reinterpret_cast<const char*>(r.args[0]));
             auto it = f.values.find(name);
             Record({0x6EA70000, Reloc(r.ecx), name, r.args[2], it != f.values.end()});
             if (it == f.values.end())
                 return 0u;
             *reinterpret_cast<uint32_t*>(r.args[1]) = it->second;
             return 1u;
         });
    s.At({0x00476B40, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t)>(&Script::CStruct::ContainsFlag))},
         4, [](Regs& r) {
             bool has = g_structs[r.ecx].flags.count(r.args[0]) != 0;
             Record({0xF1A60000, Reloc(r.ecx), r.args[0], has});
             return static_cast<uint32_t>(has);
         });
    // nodes
    s.At({0x0045C1B0, AddressOf(&SkateScript::GetNode)}, 0, [](Regs& r) {
        uint32_t node = g_nodes.count(r.args[0]) ? g_nodes[r.args[0]] : 0;
        Record({0x90DE0000, r.args[0], r.args[1] & 0xFF, Reloc(node)});
        return node;
    });
    s.At({0x0045C050, AddressOf(static_cast<bool (*)(Script::CStruct*, Mth::Vector*)>(&SkateScript::GetPosition))},
         0, [](Regs& r) {
             FakeStruct& f = g_structs[r.args[0]];
             *reinterpret_cast<Mth::Vector*>(r.args[1]) = f.hasPos ? f.pos : Mth::Vector{0, 0, 0, 1};
             Record({0x9050000, Reloc(r.args[0]), Reloc(r.args[1])});
             return static_cast<uint32_t>(f.hasPos);
         });
    s.At({0x0045C1D0, AddressOf(&SkateScript::StartNodeWalk)}, 0, [](Regs& r) {
        g_walkAt = 0;
        Record({0x3A1C0000, r.args[0]});
        return 0u;
    });
    s.At({0x0045C1F0, AddressOf(&SkateScript::NextNode)}, 0, [](Regs&) {
        uint32_t node = g_walkAt < g_walk.size() ? g_walk[g_walkAt++] : 0;
        Record({0x3A1C0001, Reloc(node)});
        return node;
    });
    s.At({0x0045C200, AddressOf(&SkateScript::EndNodeWalk)}, 0, [](Regs&) {
        Record({0x3A1C0002});
        return 0u;
    });
    s.At({0x0045BE40, AddressOf(&SkateScript::GetNodeName)}, 0, [](Regs& r) {
        uint32_t name = g_structs[r.args[0]].values[0xA1DC81F9];
        Record({0x3A1C0003, Reloc(r.args[0]), name});
        return name;
    });
    // memory
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        uint32_t block = Base() + g_bump;
        g_bump += (r.args[0] + 15) & ~15u;
        std::memset(reinterpret_cast<void*>(block), 0, r.args[0]);
        Record({0xCA110000, r.args[0], Reloc(block)});
        return block;
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xF4EE0000, Reloc(r.args[0])});
        return 0u;
    });
    // sectors
    s.At({0x0048C510, AddressOf(Nx::Engine::GetSector)}, 0, [](Regs& r) {
        uint32_t sector = 0;
        // (always one for a node whose shape is the sector's collision: without, it would be a null read)
        bool needed = g_nodes.count(r.args[0]) && g_structs[g_nodes[r.args[0]]].values[0x22CF0027] == 0x6AADF154;
        if (needed || g_answers.Below(4) != 0)
            sector = Base() + kSectors + 0x100 * g_answers.Below(4);
        Record({0x5EC70000, r.args[0], Reloc(sector)});
        return sector;
    });
    s.At({0x0049E6A0, AddressOf(&Nx::CSector::GetBoundingBox)}, 0, [](Regs& r) {
        Record({0x5EC70001, Reloc(r.ecx)});
        return r.ecx + 0x20;
    });
    s.At({0x0049E740, AddressOf(&Nx::CSector::GetOrientation)}, 0, [](Regs& r) {
        Record({0x5EC70002, Reloc(r.ecx)});
        return r.ecx + 0x40;
    });
}

void BuildWorld(Lcg& random)
{
    std::memset(g_arena, 0, kArenaSize);
    g_bump = kHeap;
    g_structs.clear();
    g_nodes.clear();
    g_walk.clear();
    g_byId.clear();
    // (the vtables written by the caller)
    for (uint32_t i = 0; i < 8; ++i) {
        uint8_t* skater = At(kSkaters + 0x100 * i);
        *reinterpret_cast<uint32_t*>(skater) = Base() + kVtable;
        *reinterpret_cast<uint32_t*>(skater + 0x14) = i;
        skater[0xBC] = static_cast<uint8_t>(random.Below(3) == 0 ? 0 : random.Next());
    }
    uint32_t skaters = random.Below(6);
    for (uint32_t i = 0; i < skaters; ++i)
        g_byId[i] = Base() + kSkaters + 0x100 * i;
    if (random.Below(4) == 0 && skaters > 1)
        g_byId.erase(1); // (a gap: the walk stops there)
    for (uint32_t i = 0; i < 4; ++i) {
        uint8_t* sector = At(kSectors + 0x100 * i);
        *reinterpret_cast<uint32_t*>(sector + 0x18) = Base() + kSectors + 0x100 * i + 0x80;
        *reinterpret_cast<uint32_t*>(sector + 0x80) = Base() + kCollisionVtable;
        auto* box = reinterpret_cast<float*>(sector + 0x20);
        for (int k = 0; k < 3; ++k) {
            box[k] = static_cast<float>(random.Below(200)) - 100.0f;
            box[4 + k] = box[k] + 10.0f + static_cast<float>(random.Below(150));
        }
        box[3] = 1.0f;
        box[7] = 1.0f;
        auto* m = reinterpret_cast<float*>(sector + 0x40);
        for (int k = 0; k < 16; ++k)
            m[k] = (static_cast<float>(random.Below(2001)) - 1000.0f) / 1000.0f;
        for (int k = 0; k < 3; ++k) {
            m[k * 4 + k] += 2.0f; // (well away from singular)
            m[k * 4 + 3] = 0.0f;
            m[12 + k] = static_cast<float>(random.Below(100)) - 50.0f;
        }
        m[15] = 1.0f;
    }
}

Mth::Vector RandomPoint(Lcg& random)
{
    Mth::Vector p;
    p.x = (static_cast<float>(random.Below(4001)) - 2000.0f) / 16.0f;
    p.y = (static_cast<float>(random.Below(4001)) - 2000.0f) / 16.0f;
    p.z = (static_cast<float>(random.Below(4001)) - 2000.0f) / 16.0f;
    static const float kW[] = {1.0f, 1.0f, 0.5f, 0.3f, 1.7f, 0.123f};
    p.w = random.Pick(kW); // (w values whose products round)
    return p;
}

void PushArena(std::vector<uint32_t>& trace)
{
    for (uint32_t i = 0; i < kArenaSize; i += 4) {
        if (i >= kVtable && i < kSkaters)
            continue; // (the vtables: thunk addresses)
        if (i >= kCollisionVtable && i < kStructs)
            continue;
        trace.push_back(Reloc(*reinterpret_cast<uint32_t*>(g_arena + i)));
    }
}

// Every proximity function: the manager made / emptied, nodes added from parameters of every kind (each shape
// checksum and an unknown one, each type, radius, ProximObject, with and without a sector, skaters holding the
// +0xBC flag or not), then updates for skaters / no object at points inside and outside (each shape: the sphere,
// the boxes, the sector box through its matrix, the sector's collision), entering and leaving (scripts, made and
// killed objects), activation by node and by name, lookups, the nearest points, and the clean-up.
void TestProxim()
{
    ObjsysTests::SaveGlobals({{0x006F2268, 4}, {0x006F0FB8, 4}});
    StandIns& s = *new StandIns();
    StandInProximWorld(s);
    // The box / matrix / vector maths (rewritten and tested before) the same in both runs: the original's calls go
    // to ours (the x87 originals round differently in the last bits).
    for (uint32_t address : {0x00405600u, 0x0041F470u, 0x00404090u, 0x004023F0u, 0x00403BB0u, 0x004025A0u,
                             0x00403C00u, 0x004033E0u}) {
        void* ours = OursAt(address);
        Check(ours != nullptr, "proximity nodes: our maths found");
        if (ours)
            s.Bytes(address, Branch(0xE9, address, ours));
    }
    // the fake vtables' entries: objects' slot 6 (MarkAsDead) and 8 (Finalize), the collision's +0x48
    void* markAsDead;
    void* finalize;
    void* contains;
    {
        markAsDead = s.Thunk(0, [](Regs& r) {
            Record({0xDEAD0000, Reloc(r.ecx)});
            return 0u;
        });
        finalize = s.Thunk(0, [](Regs& r) {
            Record({0xF1A10000, Reloc(r.ecx)});
            return 0u;
        });
        contains = s.Thunk(8, [](Regs& r) {
            auto* point = reinterpret_cast<const float*>(r.args[0]);
            auto* offset = reinterpret_cast<const float*>(r.args[1]);
            uint32_t inside = g_answers.Below(2);
            Record({0x5EC70003, Reloc(r.ecx), Bits(point[0]), Bits(point[1]), Bits(point[2]), Bits(offset[0]),
                    Bits(offset[1]), Bits(offset[2]), inside});
            return inside;
        });
    }
    static const uint32_t kShapes[] = {0xE460ABDE, 0x03F1E5A7, 0x64FBA415, 0x6AADF154, 0xAA069978, 0x12345678, 0};
    static const uint32_t kTypes[] = {0x57525413, 0x806FFF30, 0xC4E311FA, 0x00000999, 0};
    for (uint32_t seed = 1; seed <= 24; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            Lcg random(0x9E0C0000 + seed);
            g_answers = Lcg(0xA45E0000 + seed);
            BuildWorld(random);
            auto* vtable = reinterpret_cast<void**>(At(kVtable));
            for (int i = 0; i < 10; ++i)
                vtable[i] = nullptr;
            vtable[6] = markAsDead;
            vtable[8] = finalize;
            reinterpret_cast<void**>(At(kCollisionVtable))[0x48 / 4] = contains;
            GameVariable<uint32_t>(0x006F2268) = 0;
            GameVariable<uint32_t>(0x006F0FB8) = Base() + 0x80;
            Fn<void(__cdecl*)()>(original, 0x0046AB30)();
            Fn<void(__cdecl*)()>(original, 0x0046AB30)();
            // the nodes
            std::vector<uint32_t> names;
            uint32_t count = 3 + random.Below(4);
            for (uint32_t k = 0; k < count; ++k) {
                uint32_t name = 0x0DE00000 + k;
                uint32_t structure = Base() + kStructs + 0x40 * k;
                FakeStruct& f = g_structs[structure];
                f.values[0xA1DC81F9] = random.Below(5) == 0 ? name + 0x100 : name;
                if (uint32_t shape = random.Pick(kShapes))
                    f.values[0x22CF0027] = shape;
                if (uint32_t type = random.Pick(kTypes))
                    f.values[0x7321A8D6] = type;
                if (random.Below(4) != 0)
                    f.values[0xC48391A5] = Bits(static_cast<float>(5 + random.Below(60)) + 0.25f);
                if (random.Below(5) != 0)
                    f.values[0x2CA8A299] = 0x5C000000 + k;
                if (random.Below(3) == 0)
                    f.flags.insert(0x5113F19C);
                f.hasPos = random.Below(5) != 0;
                f.pos = RandomPoint(random);
                f.pos.w = 1.0f;
                g_nodes[name] = structure;
                g_nodes[f.values[0xA1DC81F9]] = structure; // (renamed: its sector is looked up by the new name)
                names.push_back(f.values[0xA1DC81F9]);
                Fn<void(__cdecl*)(uint32_t, uint32_t)>(original, 0x0046B1F0)(name, random.Below(3) != 0 ? 1 : 0);
            }
            auto* manager = GameVariable<uint32_t*>(0x006F2268);
            std::vector<uint8_t*> nodes;
            for (auto* node = reinterpret_cast<uint8_t*>(manager[0]); node;
                 node = *reinterpret_cast<uint8_t**>(node + 0xA4))
                nodes.push_back(node);
            for (int step = 0; step < 40; ++step) {
                uint8_t* node = nodes[random.Below(static_cast<uint32_t>(nodes.size()))];
                int shape = *reinterpret_cast<int*>(node + 0x6C);
                switch (random.Below(9)) {
                case 0:
                case 1:
                case 2:
                case 3: {
                    // a point near a node, or anywhere
                    Mth::Vector p = RandomPoint(random);
                    if (random.Below(2)) {
                        auto* pos = reinterpret_cast<float*>(node + 4);
                        p.x = pos[0] + static_cast<float>(random.Below(41)) - 20.0f;
                        p.y = pos[1] + static_cast<float>(random.Below(41)) - 20.0f;
                        p.z = pos[2] + static_cast<float>(random.Below(41)) - 20.0f;
                    }
                    static const uint32_t kMasks[] = {1, 2, 4, 8, 3, 0x80000000u};
                    uint32_t mask = random.Pick(kMasks);
                    uint32_t object = 0;
                    uint32_t which = random.Below(5);
                    if (which < 4)
                        object = Base() + kSkaters + 0x100 * which;
                    Fn<void(__cdecl*)(uint32_t, uint32_t, const Mth::Vector*, int)>(original, 0x0046B400)(
                        mask, object, &p, static_cast<int>(random.Below(3)));
                    break;
                }
                case 4: {
                    if (shape == 3 || shape > 4 || shape < 0)
                        break;
                    if (shape != 0 && !*reinterpret_cast<uint32_t*>(node + 0x78))
                        break; // (the sector shapes without a sector: the box and matrix were never set)
                    Mth::Vector p = RandomPoint(random);
                    Mth::Vector out{};
                    auto* result = Fn<Mth::Vector*(__thiscall*)(void*, Mth::Vector*, const Mth::Vector*)>(
                        original, 0x0046AEC0)(node, &out, &p);
                    Record({RelocPointer(result), Bits(out.x), Bits(out.y), Bits(out.z), Bits(out.w)});
                    break;
                }
                case 5: {
                    if (shape == 4 && !*reinterpret_cast<uint32_t*>(node + 0x78))
                        break; // (no sector: a null read)
                    Mth::Vector p = RandomPoint(random);
                    bool inside = Fn<bool(__thiscall*)(void*, const Mth::Vector*)>(original, 0x0046AD50)(node, &p);
                    Record({0x1251DE00u | static_cast<uint32_t>(inside)});
                    break;
                }
                case 6:
                    Fn<void(__thiscall*)(void*, uint32_t)>(original, 0x0046ABC0)(node, random.Below(2));
                    break;
                case 7: {
                    uint32_t name = random.Below(4) == 0 ? 0x77777777u : names[random.Below(count)];
                    static const int kActive[] = {0, 1, 2, -1};
                    Fn<void(__cdecl*)(uint32_t, int)>(original, 0x0046ACB0)(name, random.Pick(kActive));
                    break;
                }
                default: {
                    uint32_t name = random.Below(4) == 0 ? 0x77777777u : names[random.Below(count)];
                    Record({RelocPointer(Fn<void*(__cdecl*)(uint32_t)>(original, 0x0046AB90)(name))});
                    break;
                }
                }
            }
            // directed: each node, active and with everyone outside, entered by two skaters at its centre (one
            // leaving again far away), its sphere's edge tested, then deactivated (its objects / exit scripts)
            for (uint8_t* node : nodes) {
                auto* pos = reinterpret_cast<const Mth::Vector*>(node + 4);
                int type = *reinterpret_cast<int*>(node + 0x68);
                int shape = *reinterpret_cast<int*>(node + 0x6C);
                node[0x54] = 1;
                *reinterpret_cast<uint32_t*>(node + 0x64) = 0xFFFFFFFF;
                for (uint32_t skater = 0; skater < 2; ++skater) {
                    uint32_t object = Base() + kSkaters + 0x100 * skater;
                    Mth::Vector at = *pos;
                    Fn<void(__cdecl*)(uint32_t, uint32_t, const Mth::Vector*, int)>(original, 0x0046B400)(
                        1u << skater, object, &at, type);
                    if (skater == 1) {
                        Mth::Vector distant = {pos->x + 5000.0f, pos->y, pos->z, 1.0f};
                        Fn<void(__cdecl*)(uint32_t, uint32_t, const Mth::Vector*, int)>(original, 0x0046B400)(
                            1u << skater, object, &distant, type);
                    }
                }
                if (shape == 0) {
                    Mth::Vector edge = {pos->x + *reinterpret_cast<float*>(node + 0x70), pos->y, pos->z, 1.0f};
                    bool inside =
                        Fn<bool(__thiscall*)(void*, const Mth::Vector*)>(original, 0x0046AD50)(node, &edge);
                    Record({0xED6E0000u | static_cast<uint32_t>(inside)});
                }
                Fn<void(__thiscall*)(void*, uint32_t)>(original, 0x0046ABC0)(node, 0);
            }
            PushArena(g_trace);
            Fn<void(__cdecl*)()>(original, 0x0046AB50)();
            Record({Reloc(GameVariable<uint32_t>(0x006F2268)), Reloc(manager[0])});
            // a null manager: the clean-up and the activation by name do nothing
            GameVariable<uint32_t>(0x006F2268) = 0;
            Fn<void(__cdecl*)()>(original, 0x0046AB50)();
            Fn<void(__cdecl*)(uint32_t, int)>(original, 0x0046ACB0)(names[0], 1);
            std::vector<uint32_t> trace = g_trace;
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "proximity nodes: add, update, activate, nearest points, clean-up");
        if (expected != actual) {
            Log("  (seed %u)", seed);
            break;
        }
    }
    ObjsysTests::CleanUp();
}
SELFTEST(Early, TestProxim, ObjsysTests::CleanUp);

// The shape names, and the restart node lists: nodes of class Restart or not, with restart_types of one or more
// types or none, from a start node (skipped) or the first; the n-th of them (wrapping past the count, none).
void TestRestartNodes()
{
    bool same = true;
    for (uint32_t checksum : {0xE460ABDEu, 0x03F1E5A7u, 0x64FBA415u, 0x6AADF154u, 0xAA069978u, 0x6AADF153u,
                              0x6AADF155u, 0u, 0xFFFFFFFFu}) {
        if (Original<int(__cdecl*)(uint32_t)>(0x0046AAC0)(checksum) != Obj::ProximShapeFromChecksum(checksum))
            same = false;
    }
    Check(same, "proximity nodes: shape names");

    StandIns& s = *new StandIns();
    StandInProximWorld(s);
    for (uint32_t seed = 1; seed <= 24; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            Lcg random(0x4E570000 + seed);
            g_answers = Lcg(0xA45E0000 + seed);
            BuildWorld(random);
            uint32_t count = random.Below(9);
            for (uint32_t k = 0; k < count; ++k) {
                uint32_t structure = Base() + kStructs + 0x40 * k;
                FakeStruct& f = g_structs[structure];
                f.values[0xA1DC81F9] = 0x0DE00000 + k;
                if (random.Below(4) != 0)
                    f.values[0x12B4E660] = random.Below(4) == 0 ? 0x11111111u : 0x1806DDF8u;
                if (random.Below(3) != 0) {
                    // restart_types
                    uint8_t* array = At(kArrays + 0x40 * k);
                    uint32_t size = 1 + random.Below(3);
                    array[1] = 0x0D;
                    *reinterpret_cast<uint32_t*>(array + 4) = size;
                    uint32_t* elements = reinterpret_cast<uint32_t*>(array + 0x10);
                    for (uint32_t i = 0; i < size; ++i)
                        elements[i] = 0x7E000000 + random.Below(3);
                    if (size == 1)
                        *reinterpret_cast<uint32_t*>(array + 8) = elements[0];
                    else
                        *reinterpret_cast<uint32_t**>(array + 8) = elements;
                    f.values[Crc::GenerateCRCFromString("restart_types")] = reinterpret_cast<uint32_t>(array);
                }
                g_walk.push_back(structure);
            }
            for (int call = 0; call < 6; ++call) {
                uint32_t type = random.Below(3) == 0 ? 0 : 0x7E000000 + random.Below(3);
                uint32_t start = random.Below(2) ? 0 : 0x0DE00000;
                uint32_t out[64];
                std::memset(out, 0xEE, sizeof(out));
                int found = Fn<int(__cdecl*)(uint32_t, uint32_t*, uint32_t)>(original, 0x0046B6C0)(type, out, start);
                Record({static_cast<uint32_t>(found)});
                for (int i = 0; i < found && i < 64; ++i)
                    Record({out[i]});
                uint32_t asked = 0x0DE00000 + random.Below(count + 1);
                g_nodes[asked] = Base() + kStructs + 0x40 * (asked - 0x0DE00000);
                Record({Fn<uint32_t(__cdecl*)(uint32_t)>(original, 0x0046B680)(asked) & 0xFF});
                int index = static_cast<int>(random.Below(12));
                Record({Fn<uint32_t(__cdecl*)(uint32_t, int, uint32_t)>(original, 0x0046B790)(type, index, start)});
            }
            std::vector<uint32_t> trace = g_trace;
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "restart nodes: lists by type, from a start node, the n-th");
        if (expected != actual) {
            Log("  (seed %u)", seed);
            break;
        }
    }
    RemoveStandIns();
}
SELFTEST(Early, TestRestartNodes, RemoveStandIns);

// The preferences: every method on a stand-in structure whose getters find what's asked or not (and write a
// value when they do): constructed, destroyed (deleting or not), each preference getter (found field, missing
// field: the name comes back), the setters (preference / array there or not), the loads.
void TestPreferences()
{
    StandIns& s = *new StandIns();
    s.At({0x00476410, AddressOf(&Script::CStruct::Init)}, 0, [](Regs& r) {
        Record({0x1A170000, Reloc(r.ecx)});
        return r.ecx;
    });
    s.At({0x00477130, AddressOf(&Script::CStruct::Clear)}, 0, [](Regs& r) {
        Record({0xC1EA0000, Reloc(r.ecx)});
        return 0u;
    });
    // a getter: found or not (by the next answer), writing `value` when found
    auto getter = [](uint32_t tag, uint32_t value) {
        return [tag, value](Regs& r) {
            uint32_t found = g_answers.Below(3) != 0;
            Record({tag, Reloc(r.ecx), r.args[0], r.args[2] & 0xFF, found});
            if (found)
                *reinterpret_cast<uint32_t*>(r.args[1]) = value + r.args[0];
            return found;
        };
    };
    s.At({0x004767C0, 0x00476E70,
          AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, Script::CStruct**, int)>(
              &Script::CStruct::GetStructure))},
         12, getter(0x6E750000, Base() + 0x100));
    s.At({0x00476860, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, Script::CArray**, int)>(
                          &Script::CStruct::GetArray))},
         12, getter(0x6EA00000, Base() + 0x200));
    s.At({0x00476C60, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, const char**, int)>(
                          &Script::CStruct::GetString))},
         12, getter(0x6E570000, 0x5000));
    s.At({0x00476CB0, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, int*, int)>(
                          &Script::CStruct::GetInteger))},
         12, getter(0x6E140000, 0x6000));
    s.At({0x00476EC0, 0x00476950,
          AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, uint32_t*, int)>(&Script::CStruct::GetChecksum))},
         12, getter(0x6EC50000, 0x7000));
    s.At({0x00478540, AddressOf(&Script::CStruct::AppendStructureIfAny)}, 4, [](Regs& r) {
        Record({0xA99E0000, Reloc(r.ecx), Reloc(r.args[0])});
        return 0u;
    });
    s.At({0x00478B00, AddressOf(static_cast<void (Script::CStruct::*)(uint32_t, Script::CArray*)>(
                          &Script::CStruct::AddArray))},
         8, [](Regs& r) {
             Record({0xADDA0000, Reloc(r.ecx), r.args[0], Reloc(r.args[1])});
             return 0u;
         });
    s.At({0x00479020, AddressOf(static_cast<Script::CStruct* (*)(uint32_t)>(&Script::GetStructure))}, 0,
         [](Regs& r) {
             Record({0x65750000, r.args[0]});
             return Base() + 0x300 + (r.args[0] & 0xF0);
         });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xF4EE0000, Reloc(r.args[0])});
        return 0u;
    });
    for (uint32_t seed = 1; seed <= 6; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            std::memset(g_arena, 0xCD, 0x400);
            g_answers = Lcg(0x93EF0000 + seed);
            Lcg random(0x93E00000 + seed);
            void* prefs = g_arena;
            Record({RelocPointer(Fn<void*(__thiscall*)(void*)>(original, 0x0046C470)(prefs))});
            for (int step = 0; step < 30; ++step) {
                uint32_t name = 0x0A000000 + random.Below(16) * 0x10;
                uint32_t field = 0x0F000000 + random.Below(16);
                switch (random.Below(10)) {
                case 0:
                    Record({RelocPointer(Fn<void*(__thiscall*)(void*, uint32_t)>(original, 0x0046C4A0)(prefs, name))});
                    break;
                case 1:
                    Record({RelocPointer(Fn<void*(__thiscall*)(void*, uint32_t)>(original, 0x0046C4D0)(prefs, name))});
                    break;
                case 2:
                    Fn<void(__thiscall*)(void*, uint32_t)>(original, 0x0046C500)(prefs, Base() + 0x380);
                    break;
                case 3: {
                    // (missing preference: a null structure's getter - stood in, so no crash)
                    Record({Reloc(Fn<uint32_t(__thiscall*)(void*, uint32_t, uint32_t)>(original, 0x0046C510)(
                        prefs, name, field))});
                    break;
                }
                case 4:
                    Record({Reloc(Fn<uint32_t(__thiscall*)(void*, uint32_t, uint32_t)>(original, 0x0046C550)(
                        prefs, name, field))});
                    break;
                case 5:
                    Record({Reloc(Fn<uint32_t(__thiscall*)(void*, uint32_t, uint32_t)>(original, 0x0046C590)(
                        prefs, name, field))});
                    break;
                case 6:
                    Record({Fn<uint32_t(__thiscall*)(void*, uint32_t, uint32_t)>(original, 0x0046C5D0)(
                                prefs, name, Base() + 0x3C0) &
                            0xFF});
                    break;
                case 7:
                    Record({Fn<uint32_t(__thiscall*)(void*, uint32_t, uint32_t)>(original, 0x0046C610)(
                                prefs, name, Base() + 0x3E0) &
                            0xFF});
                    break;
                case 8:
                    Record({Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x0046C650)(prefs, name) & 0xFF});
                    break;
                default:
                    Fn<void(__thiscall*)(void*)>(original, 0x0046C490)(prefs);
                    break;
                }
            }
            for (uint32_t flags : {0u, 1u, 2u, 3u})
                Record({RelocPointer(Fn<void*(__thiscall*)(void*, uint32_t)>(original, 0x0046C680)(prefs, flags))});
            for (uint32_t i = 0; i < 0xC; i += 4)
                Record({Reloc(*reinterpret_cast<uint32_t*>(g_arena + i))});
            std::vector<uint32_t> trace = g_trace;
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "preferences: getters, setters, loads, destruction");
        if (expected != actual) {
            Log("  (seed %u)", seed);
            break;
        }
    }
    RemoveStandIns();
}
SELFTEST(Early, TestPreferences, RemoveStandIns);

} // namespace
