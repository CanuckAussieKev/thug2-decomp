// Self-tests for CCompositeObject and CBaseComponent (src/game/obj/composite.cpp): the original and ours run the
// same calls on identical stand-in objects and components in an arena. Everything they call outside the batch
// (the structure functions, CObject's methods, the script's Update, the composite object manager, the memory
// manager, the objects' and components' virtual functions) is stood in for by recording code (written over the
// game's function and ours alike), so the two are compared call by call, return value by return value, then the
// whole arena word by word.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <tuple>
#include <vector>

#include "game/core/manager.h"
#include "game/mth/matrix.h"
#include "game/obj/compositemgr.h"
#include "game/obj/object.h"
#include "game/core/crc.h"
#include "game/core/mem.h"
#include "game/obj/path_manager.h"
#include "game/script/cscript.h"
#include "game/script/node_manager.h"
#include "game/script/node_prefixes.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using Obj::CBaseComponent;
using Obj::CCompositeObject;
using Obj::CObject;
using Script::CArray;
using Script::CMemberFunctionEntry;
using Script::CScript;
using Script::CStruct;

constexpr uint32_t kArenaSize = 0x30000; // (the node manager is 0x100FC bytes)
alignas(16) uint8_t g_cobj_arenas[2][kArenaSize];
uint32_t g_cobj_base;
uint32_t g_cobj_bump;
bool g_cobj_original;
uint32_t g_cobj_killOn;      // the stand-in update (0x5C: the script's; else a component, arena-relative) that kills
CObject* g_cobj_victim;      // the object it kills
uint32_t g_cobj_found;       // what the stand-in FindObject returns (arena-relative or 0)
uint32_t g_cobj_nextType;    // the type of the next component the stand-in manager makes (0: none made)
std::vector<Saved> g_cobj_saved;
std::map<std::tuple<char, uint32_t, uint32_t>, std::vector<uint32_t>> g_cobj_answers;

// (both arenas, the same for both runs: a value that only happens to fall inside one of them maps the same way in
// each - ArenaWord, selftest.h)
bool InArena(uint32_t word) { return word - reinterpret_cast<uint32_t>(g_cobj_arenas) <= 2 * kArenaSize; }
uint32_t Rel(uint32_t word) { return ArenaWord(g_cobj_arenas, kArenaSize, word); }
uint32_t Rel(const void* p) { return Rel(reinterpret_cast<uint32_t>(p)); }
void Rec(uint32_t tag, std::initializer_list<uint32_t> values)
{
    g_trace.push_back(tag);
    for (uint32_t v : values)
        g_trace.push_back(Rel(v));
}
void Result(uint32_t value) { Rec(0x2E5, {value}); }
uint32_t Bits(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}
uint32_t Bump(uint32_t size, uint8_t fill)
{
    uint32_t block = g_cobj_bump;
    g_cobj_bump += (size + 15) & ~15u;
    std::memset(reinterpret_cast<void*>(block), fill, size);
    return block;
}
// A stand-in structure's answer: kind 'K' checksum, 'F' float, 'V' vector (4 words), 'S' string, 'L' flag.
void Answer(char kind, const void* structure, uint32_t name, std::vector<uint32_t> value)
{
    g_cobj_answers[{kind, Rel(structure), name}] = std::move(value);
}
const std::vector<uint32_t>* Lookup(char kind, uint32_t structure, uint32_t name)
{
    auto it = g_cobj_answers.find({kind, Rel(structure), name});
    return it == g_cobj_answers.end() ? nullptr : &it->second;
}

void Cleanup()
{
    RemoveStandIns();
    RestoreAll(g_cobj_saved);
    g_cobj_saved.clear();
}

void* g_cobj_componentVtable[13];
void* g_cobj_objectVtable[11];
void* g_cobj_memberCode; // a member function a table entry points to
void* g_cobj_managerVtable[8];
uint32_t g_cobj_listHead;  // what the stand-in manager's object list is
float g_cobj_angle;        // what the stand-in GetAngleInPlane returns
bool g_cobj_allocFail;     // the stand-in Calloc fails
std::map<uint32_t, uint32_t> g_cobj_symbolArrays; // the stand-in global arrays (name -> array)
float g_cobj_quatW;        // the w of the quaternion the stand-in MatrixToQuat makes

// GetAngleInPlane returns its float in ST0: a C++ stand-in, jumped to from the original's start and ours.
float __cdecl StandInAngleInPlane(const Mth::Matrix* m, const Mth::Vector* v, int ref, int normal)
{
    Rec(0x62D0, {reinterpret_cast<uint32_t>(m), InArena(reinterpret_cast<uint32_t>(v)) ? 1u : 0x57ACu, Bits(v->x), Bits(v->y), Bits(v->z), Bits(v->w),
                 static_cast<uint32_t>(ref), static_cast<uint32_t>(normal)});
    return g_cobj_angle;
}

// Our stand-ins kill the object for the update that `g_cobj_killOn` names.
void MaybeKill(uint32_t who)
{
    if (g_cobj_killOn && Rel(who) == g_cobj_killOn)
        g_cobj_victim->m_objectFlags |= Obj::OBJECT_FLAG_DEAD;
}

void InstallStandIns(StandIns& s)
{
    // --- the structure functions ---
    auto getter = [&](std::initializer_list<uint32_t> addresses, char kind, uint32_t tag) {
        s.At(addresses, 12, [kind, tag](Regs& r) {
            Rec(tag, {r.ecx, r.args[0], r.args[2] & 0xFF});
            const std::vector<uint32_t>* value = Lookup(kind, r.ecx, r.args[0]);
            if (!value)
                return 0u;
            std::memcpy(reinterpret_cast<void*>(r.args[1]), value->data(), value->size() * 4);
            return 1u;
        });
    };
    getter({0x00476950, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))},
           'K', 0x6E50);
    getter({0x004766D0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, float*, int)>(&CStruct::GetFloat))}, 'F',
           0x6E51);
    getter({0x00476FB0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, Script::VectorValue*, int)>(
                            &CStruct::GetVector))},
           'V', 0x6E52);
    getter({0x00476C60, AddressOf(static_cast<bool (CStruct::*)(uint32_t, const char**, int)>(&CStruct::GetString))},
           'S', 0x6E53);
    s.At({0x00476B40, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsFlag))}, 4, [](Regs& r) {
        Rec(0x6E54, {r.ecx, r.args[0]});
        return Lookup('L', r.ecx, r.args[0]) ? 1u : 0u;
    });
    s.At({0x00477E20, AddressOf(static_cast<void (CStruct::*)(uint32_t, float, float, float)>(&CStruct::AddVector))},
         16, [](Regs& r) {
             Rec(0x6E60, {r.ecx, r.args[0], r.args[1], r.args[2], r.args[3]});
             return 0u;
         });
    s.At({0x00477C60, AddressOf(static_cast<void (CStruct::*)(uint32_t, float)>(&CStruct::AddFloat))}, 8,
         [](Regs& r) {
             Rec(0x6E61, {r.ecx, r.args[0], r.args[1]});
             return 0u;
         });
    s.At({0x00477D40, AddressOf(static_cast<void (CStruct::*)(uint32_t, uint32_t)>(&CStruct::AddChecksum))}, 8,
         [](Regs& r) {
             Rec(0x6E62, {r.ecx, r.args[0], r.args[1]});
             return 0u;
         });
    s.At({0x00478480, AddressOf(&CStruct::AppendStructure)}, 4, [](Regs& r) {
        Rec(0x6E63, {r.ecx, r.args[0]});
        return r.ecx;
    });
    s.At({0x00476410, AddressOf(&CStruct::Init)}, 0, [](Regs& r) {
        Rec(0x6E64, {r.ecx});
        reinterpret_cast<uint32_t*>(r.ecx)[0] = 0x1717;
        reinterpret_cast<uint32_t*>(r.ecx)[1] = 0;
        return r.ecx;
    });
    s.At({0x00477130, AddressOf(&CStruct::Clear)}, 0, [](Regs& r) {
        Rec(0x6E65, {r.ecx});
        return 0u;
    });
    s.At({0x00409070, AddressOf(&Script::DeleteStruct)}, 0, [](Regs& r) {
        Rec(0x6E66, {r.args[0]});
        return 0u;
    });
    s.At({0x0046CB00, AddressOf(&CArray::GetArrayPointer)}, 0, [](Regs& r) {
        Rec(0x6E67, {r.ecx});
        return r.ecx + 0x10; // (the stand-in arrays keep their elements right after them)
    });
    // A new structure: the original's inlined pool allocation (0x005BA3D0 on the pool 0x006F361C), ours NewStruct.
    s.At({0x005BA3D0}, 0, [](Regs& r) {
        Rec(0x6E68, {});
        (void)r;
        return Bump(8, 0xEE);
    });
    s.At({AddressOf(&Script::NewStruct)}, 0, [](Regs& r) {
        Rec(0x6E68, {});
        (void)r;
        return Bump(8, 0xEE);
    });
    // --- CObject and the script ---
    s.At({0x00460B80, AddressOf(&CObject::Construct)}, 0, [](Regs& r) {
        Rec(0x0B80, {r.ecx});
        reinterpret_cast<CObject*>(r.ecx)->m_objectFlags = 0x100;
        return r.ecx;
    });
    s.At({0x004615D0, AddressOf(&CObject::DestroyBody)}, 0, [](Regs& r) {
        Rec(0x15D0, {r.ecx, *reinterpret_cast<uint32_t*>(r.ecx)});
        return 0u;
    });
    s.At({0x00460A50, AddressOf(&CObject::MarkAsDead)}, 0, [](Regs& r) {
        Rec(0x0A50, {r.ecx});
        return 0u;
    });
    s.At({0x00461930, AddressOf(&CObject::SetID)}, 4, [](Regs& r) {
        Rec(0x1930, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x004608F0, AddressOf(static_cast<bool (CObject::*)(uint32_t, CStruct*, CScript*)>(
                           &CObject::CallMemberFunction))},
         12, [](Regs& r) {
             Rec(0x08F0, {r.ecx, r.args[0], r.args[1], r.args[2]});
             return 0x5A01u;
         });
    s.At({0x00460AD0, AddressOf(&Obj::FindObject)}, 0, [](Regs& r) {
        Rec(0x0AD0, {r.args[0]});
        return g_cobj_found ? g_cobj_base + (g_cobj_found & 0xFFFF) : 0u;
    });
    s.At({0x004750B0, AddressOf(&CScript::Update)}, 0, [](Regs& r) {
        Rec(0x50B0, {r.ecx});
        MaybeKill(0x5C);
        return 2u;
    });
    s.At({0x00472E70, AddressOf(&Script::ComponentDestroyedAllScripts)}, 0, [](Regs& r) {
        Rec(0x2E70, {r.args[0]});
        return 0u;
    });
    // --- the composite object manager (wt/coim), the position getters, the memory manager ---
    s.At({0x0045F3E0, AddressOf(&Obj::CCompositeObjectManager::AddComponentByType)}, 4, [](Regs& r) {
        Rec(0xF3E0, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x0045F420, AddressOf(&Obj::CCompositeObjectManager::RemoveComponentByType)}, 4, [](Regs& r) {
        Rec(0xF420, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x0045F2A0, AddressOf(&Obj::CCompositeObjectManager::CreateComponent)}, 4, [](Regs& r) {
        Rec(0xF2A0, {r.ecx, r.args[0]});
        if (!g_cobj_nextType)
            return 0u;
        auto* component = reinterpret_cast<CBaseComponent*>(Bump(0x20, 0));
        component->m_vtable = g_cobj_componentVtable;
        component->m_type = g_cobj_nextType;
        return reinterpret_cast<uint32_t>(component);
    });
    s.At({0x0045C050}, 0, [](Regs& r) {
        Rec(0xC050, {r.args[0], InArena(r.args[1]) ? r.args[1] : 0x57ACu});
        const float pos[4] = {7.5f, -3.25f, 100.0625f, 1.0f};
        std::memcpy(reinterpret_cast<void*>(r.args[1]), pos, sizeof(pos));
        return 0u;
    });
    s.At({0x0045C1B0}, 0, [](Regs& r) {
        Rec(0xC1B0, {r.args[0], r.args[1]});
        return g_cobj_base + 0x3F00;
    });
    s.At({0x0042BC40}, 4, [](Regs& r) {
        const auto* q = reinterpret_cast<const uint32_t*>(r.ecx);
        Rec(0xBC40, {q[0], q[1], q[2], q[3], InArena(r.ecx) ? r.ecx : 0x57ACu, r.args[0]});
        auto* m = reinterpret_cast<float*>(r.args[0]);
        for (int i = 0; i < 16; ++i)
            m[i] = 0.25f * static_cast<float>(i) - 1.0f;
        return 0u;
    });
    s.At({0x0042E1F0}, 4, [](Regs& r) {
        Rec(0xE1F0, {InArena(r.ecx) ? r.ecx : 0x57ACu, r.args[0]});
        const float q[4] = {0.125f, -0.5f, 0.75f, g_cobj_quatW};
        std::memcpy(reinterpret_cast<void*>(r.ecx), q, sizeof(q));
        return r.ecx;
    });
    s.At({0x004048D0, AddressOf(&Mth::Matrix::SetFromEulerAngles)}, 12, [](Regs& r) {
        Rec(0x48D0, {InArena(r.ecx) ? r.ecx : 0x57ACu, r.args[0], r.args[1], r.args[2]});
        auto* m = reinterpret_cast<uint32_t*>(r.ecx);
        for (uint32_t i = 0; i < 16; ++i)
            m[i] = r.args[i % 3] ^ i;
        return r.ecx;
    });
    for (uint32_t address : {0x004062D0u, AddressOf(&Mth::GetAngleInPlane)})
        s.Bytes(address, Branch(0xE9, address, reinterpret_cast<void*>(&StandInAngleInPlane)));
    s.At({0x0045C0B0}, 0, [](Regs& r) {
        Rec(0xC0B0, {r.args[0], r.args[1]});
        return 0u;
    });
    // The node manager's helpers: the arrays, the hash functions and node functions before it, sprintf, Calloc.
    s.At({0x0046CC30, AddressOf(&CArray::GetElement)}, 4, [](Regs& r) {
        Rec(0xCC30, {r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t*>(r.ecx + 0x10)[r.args[0]];
    });
    s.At({0x0046CB10, AddressOf(&CArray::SetElement)}, 8, [](Regs& r) {
        Rec(0xCB10, {r.ecx, r.args[0], r.args[1]});
        reinterpret_cast<uint32_t*>(r.ecx + 0x10)[r.args[0]] = r.args[1];
        return 0u;
    });
    s.At({0x0046CAF0, AddressOf(&CArray::SetType)}, 4, [](Regs& r) {
        Rec(0xCAF0, {r.ecx, r.args[0] & 0xFF});
        return 0u;
    });
    getter({0x00476860, AddressOf(static_cast<bool (CStruct::*)(uint32_t, CArray**, int)>(&CStruct::GetArray))}, 'A',
           0x6E55);
    s.At({0x004776B0, AddressOf(static_cast<void (CStruct::*)(uint32_t)>(&CStruct::RemoveComponent))}, 4,
         [](Regs& r) {
             Rec(0x76B0, {r.ecx, r.args[0]});
             return 0u;
         });
    s.At({0x00479070, AddressOf(static_cast<CArray* (*)(uint32_t)>(&Script::GetArray))}, 0, [](Regs& r) {
        Rec(0x9070, {r.args[0]});
        auto it = g_cobj_symbolArrays.find(r.args[0]);
        return it == g_cobj_symbolArrays.end() ? 0u : it->second;
    });
    s.At({0x0045CE40, AddressOf(&SkateScript::RemoveNodeArrayPrefixes)}, 0, [](Regs& r) {
        Rec(0xCE40, {r.args[0]});
        return 0u;
    });
    s.At({0x0045CE60, AddressOf(&SkateScript::CNodeManager::HashNode)}, 12, [](Regs& r) {
        Rec(0xCE60, {r.ecx, r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    s.At({0x0045CEF0, AddressOf(&SkateScript::CNodeManager::UnhashNodeArray)}, 4, [](Regs& r) {
        Rec(0xCEF0, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x0045C900, AddressOf(&SkateScript::AddNodeArrayPrefixes)}, 0, [](Regs& r) {
        Rec(0xC900, {r.args[0]});
        return 0u;
    });
    s.At({0x0045BE40, AddressOf(&SkateScript::GetNodeName)}, 0, [](Regs& r) {
        Rec(0xBE40, {r.args[0]});
        return 0xBE400000u + (Rel(r.args[0]) & 0xFFFF);
    });
    s.At({0x004694E0, AddressOf(&Obj::CPathManager::Instance)}, 0, [](Regs& r) {
        Rec(0x94E0, {});
        (void)r;
        return g_cobj_base + 0x2B00;
    });
    s.At({0x004696C0}, 0, [](Regs& r) {
        Rec(0x96C0, {r.ecx});
        return 0u;
    });
    s.At({0x006234FE}, 0, StandInSprintf); // the game's sprintf (not started offline)
    s.At({0x00401D40, AddressOf(static_cast<void* (*)(unsigned int, Mem::Heap*, int)>(&Mem::Calloc))}, 0,
         [](Regs& r) {
             Rec(0x1D40, {r.args[0], r.args[1], r.args[2]});
             return g_cobj_allocFail ? 0u : Bump(r.args[0], 0);
         });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Rec(0xBBA0, {r.ecx, r.args[0]});
        return 0u;
    });

    // --- the components' virtual functions (+0x18 of a stand-in component: what +0x30 returns) ---
    for (void*& slot : g_cobj_componentVtable)
        slot = nullptr;
    const uint16_t pops[13] = {4, 0, 4, 0, 0, 0, 8, 4, 4, 0, 0, 12, 12};
    for (int i = 0; i < 13; ++i) {
        if (i == 11)
            continue; // (+0x2C: CBaseComponent::CallMemberFunction itself, set below)
        g_cobj_componentVtable[i] = s.Thunk(pops[i], [i](Regs& r) {
            g_trace.push_back(0xC0000 + i);
            g_trace.push_back(Rel(r.ecx));
            if (i == 0 || i == 2 || i == 7 || i == 8)
                g_trace.push_back(Rel(r.args[0]));
            if (i == 12) {
                for (int a = 0; a < 3; ++a)
                    g_trace.push_back(Rel(r.args[a]));
                return reinterpret_cast<uint32_t*>(r.ecx)[6];
            }
            if (i == 1)
                MaybeKill(r.ecx);
            return i == 0 ? r.ecx : 0u;
        });
    }
    // --- the object's: slot 3 (a member function by name), 9 (a component from a structure), 10 (its update) ---
    for (void*& slot : g_cobj_objectVtable)
        slot = nullptr;
    g_cobj_objectVtable[3] = s.Thunk(12, [](Regs& r) {
        Rec(0x0B03, {r.ecx, r.args[0], r.args[1], r.args[2]});
        return 0x77000001u;
    });
    g_cobj_objectVtable[9] = s.Thunk(8, [](Regs& r) {
        Rec(0x0B09, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    g_cobj_objectVtable[10] = s.Thunk(0, [](Regs& r) {
        Rec(0x0B0A, {r.ecx});
        return 0u;
    });
    for (void*& slot : g_cobj_managerVtable)
        slot = nullptr;
    g_cobj_managerVtable[6] = s.Thunk(0, [](Regs& r) {
        Rec(0x3A06, {r.ecx});
        return g_cobj_listHead;
    });
    g_cobj_memberCode = s.Thunk(8, [](Regs& r) {
        Rec(0x3E3B, {r.ecx, r.args[0], r.args[1]});
        return 0x66000101u;
    });
}

void SaveGlobals()
{
    g_cobj_saved = {
        {0x007D1800, 4, {}},  // the memory manager
        {0x007CC979, 1, {}},  // the memory manager is ready
        {0x006F3614, 16, {}}, // the CStruct pools
        {0x006F0FB8, 4, {}},  // the composite object manager
        {0x006F0D84, 8, {}},  // the node manager and its references
    };
    SaveAll(g_cobj_saved);
}

uint8_t* BeginRun(bool original)
{
    g_cobj_original = original;
    g_trace.clear();
    g_cobj_answers.clear();
    uint8_t* arena = g_cobj_arenas[original ? 0 : 1];
    std::memset(arena, 0, kArenaSize);
    std::memset(arena + 0x8000, 0xDD, kArenaSize - 0x8000);
    g_cobj_base = reinterpret_cast<uint32_t>(arena);
    g_cobj_bump = g_cobj_base + 0x8000;
    g_cobj_killOn = 0;
    g_cobj_victim = nullptr;
    g_cobj_found = 0;
    g_cobj_nextType = 0;
    g_cobj_allocFail = false;
    g_cobj_symbolArrays.clear();
    for (Saved& saved : g_cobj_saved)
        std::memset(reinterpret_cast<void*>(saved.address), 0, saved.size);
    GameVariable<bool>(0x007CC979) = true;
    GameVariable<uint8_t*>(0x007D1800) = arena + 0x6000;
    GameVariable<uint8_t*>(0x006F0FB8) = arena + 0x6100;
    GameVariable<uint8_t*>(0x006F361C) = arena + 0x6200; // (the struct pool: the original allocates from it)
    return arena;
}

std::vector<uint32_t> EndRun(uint8_t* arena)
{
    std::vector<uint32_t> result = g_trace;
    std::vector<uint32_t> image(kArenaSize / 4);
    std::memcpy(image.data(), arena, kArenaSize);
    for (uint32_t& word : image)
        word = Rel(word);
    result.insert(result.end(), image.begin(), image.end());
    return result;
}

template <typename R, typename F, typename... A>
R CallC(uint32_t address, F ours, A... args)
{
    void* code = g_cobj_original ? reinterpret_cast<void*>(address) : reinterpret_cast<void*>(AddressOf(ours));
    return reinterpret_cast<R(__cdecl*)(A...)>(code)(args...);
}
template <typename R, typename F, typename... A>
R Call(uint32_t address, F ours, void* self, A... args)
{
    void* code = g_cobj_original ? reinterpret_cast<void*>(address) : reinterpret_cast<void*>(AddressOf(ours));
    return reinterpret_cast<R(__thiscall*)(void*, A...)>(code)(self, args...);
}
// The member functions scripts call: (params, script), the result's low byte (they return AL).
template <typename F>
void Member(uint32_t address, F ours, CCompositeObject* object, CStruct* params, CScript* script)
{
    uint32_t result = Call<uint32_t>(address, ours, object, params, script);
    Result(result & 0xFF);
}

CBaseComponent* MakeComponent(uint8_t* arena, int i, uint32_t type, uint32_t flags, uint32_t answer)
{
    auto* component = reinterpret_cast<CBaseComponent*>(arena + 0x1000 + 0x40 * i);
    component->m_vtable = g_cobj_componentVtable;
    component->m_type = type;
    component->m_flags = flags;
    component->m_object = nullptr;
    component->mp_next = nullptr;
    component->mp_nextOfType = reinterpret_cast<CBaseComponent*>(0x0BADF00D);
    reinterpret_cast<uint32_t*>(component)[6] = answer;
    return component;
}
CStruct* Struct(uint8_t* arena, int i) { return reinterpret_cast<CStruct*>(arena + 0x3000 + 0x10 * i); }

// --- the object's life, its frame, its components -------------------------------------------------------------
std::vector<uint32_t> RunLifeTest(bool original)
{
    uint8_t* arena = BeginRun(original);
    using C = CCompositeObject;
    auto* a = reinterpret_cast<C*>(arena + 0x100);
    std::memset(a, 0xDD, sizeof(C));
    Result(Rel(Call<C*>(0x0045E510, &C::Construct, a)));
    a->m_vtable = reinterpret_cast<void**>(g_cobj_objectVtable);
    a->m_script = reinterpret_cast<CScript*>(arena + 0x2000);

    // Four components added (the second suspended).
    CBaseComponent* c[4];
    for (int i = 0; i < 4; ++i) {
        c[i] = MakeComponent(arena, i, 0x7000 + i, i == 1 ? 1u : 0u, 2);
        Call<void>(0x0045DA30, &C::AddComponent, a, c[i]);
    }
    Result(Rel(Call<CBaseComponent*>(0x0045DB90, &C::GetComponent, a, 0x7002u)));

    // Update: plain; paused; suspended; no script; killed by its script; killed by a component.
    auto update = [&](uint32_t flags, uint32_t killOn) {
        a->m_compositeFlags = flags | Obj::COMPOSITE_FLAG_TELEPORTED;
        a->m_pos.w = 5.0f;
        a->m_objectFlags &= ~Obj::OBJECT_FLAG_DEAD;
        g_cobj_killOn = killOn;
        g_cobj_victim = a;
        Call<void>(0x0045D920, &C::Update, a);
        Rec(0xD920, {a->m_compositeFlags, Bits(a->m_pos.w), a->m_objectFlags});
        g_cobj_killOn = 0;
    };
    update(0, 0);
    update(Obj::COMPOSITE_FLAG_PAUSED, 0);
    update(Obj::COMPOSITE_FLAG_SUSPENDED | 0x40, 0);
    update(0, 0x5C);
    update(Obj::COMPOSITE_FLAG_SUSPENDED, Rel(c[2]));
    update(0, Rel(c[0]));
    a->m_script = nullptr;
    update(0, 0);
    a->m_objectFlags &= ~Obj::OBJECT_FLAG_DEAD;

    // Hide / Suspend: on, on again (no change), off; SetTeleported; ShouldUpdateScripts.
    a->m_compositeFlags = 0x10;
    for (bool on : {true, true, false, false}) {
        Call<void>(0x0045D990, &C::Hide, a, on);
        Result(a->m_compositeFlags);
    }
    for (bool on : {true, true, false, false}) {
        Call<void>(0x0045D9E0, &C::Suspend, a, on);
        Result(a->m_compositeFlags);
    }
    Call<void>(0x0045E4C0, &C::SetTeleported, a, false);
    Result(a->m_compositeFlags);
    a->m_compositeFlags = 0;
    Call<void>(0x0045E4C0, &C::SetTeleported, a, true);
    for (uint32_t flags : {0u, 1u, 0xFEu, 0xFFu}) {
        a->m_compositeFlags = flags;
        Result(Call<uint32_t>(0x0045E500, &C::ShouldUpdateScripts, a) & 0xFF);
    }
    a->m_compositeFlags = 0;
    Call<void>(0x0045D7C0, &C::MarkAsDead, a);
    Call<void>(0x0045DB30, &C::Finalize, a);
    Result(a->m_compositeFlags);

    // A member function by name: the third component says true; the second false; none knows it.
    CStruct* params = Struct(arena, 1);
    auto* script = reinterpret_cast<CScript*>(arena + 0x2000);
    using ByName = bool (C::*)(uint32_t, CStruct*, CScript*);
    for (uint32_t answers : {0x2212u, 0x2202u, 0x2222u, 0x1222u}) {
        for (int i = 0; i < 4; ++i)
            reinterpret_cast<uint32_t*>(c[i])[6] = (answers >> (12 - 4 * i)) & 0xF;
        Result(Call<uint32_t>(0x0045E450, static_cast<ByName>(&C::CallMemberFunction), a, 0xFACEu, params, script) &
               0xFF);
    }
    a->mp_components = nullptr;
    Result(Call<uint32_t>(0x0045E450, static_cast<ByName>(&C::CallMemberFunction), a, 0xFACFu, params, script) & 0xFF);

    // A table entry: with a member function pointer (and an adjustment); without (by name, slot 3).
    using ByEntry = bool (CObject::*)(CMemberFunctionEntry*, CStruct*, CScript*);
    auto* entry = reinterpret_cast<CMemberFunctionEntry*>(arena + 0x2800);
    *entry = {0xE1E1, 0x0ADA4E74, g_cobj_memberCode, 0x24, 0, 0};
    Result(Call<uint32_t>(0x0045D850, static_cast<ByEntry>(&CObject::CallMemberFunction), a, entry, params, script) & 0xFF);
    entry->mThisAdjustment = -8;
    Result(Call<uint32_t>(0x0045D850, static_cast<ByEntry>(&CObject::CallMemberFunction), a, entry, params, script) & 0xFF);
    entry->mCode = nullptr;
    Result(Call<uint32_t>(0x0045D850, static_cast<ByEntry>(&CObject::CallMemberFunction), a, entry, params, script) & 0xFF);
    // A component's: by the entry's name.
    using CompEntry = int (CBaseComponent::*)(CMemberFunctionEntry*, CStruct*, CScript*);
    reinterpret_cast<uint32_t*>(c[3])[6] = 7;
    Result(Call<uint32_t>(0x0045D720, static_cast<CompEntry>(&CBaseComponent::CallMemberFunction), c[3], entry,
                          params, script));

    // CBaseComponent: constructor (dirty memory), destructor body (a script waited / not), Destroy, Suspend.
    auto* d = reinterpret_cast<CBaseComponent*>(arena + 0x1800);
    std::memset(d, 0xDD, 0x20);
    Result(Rel(Call<CBaseComponent*>(0x0045D730, &CBaseComponent::Construct, d)));
    Result(d->m_flags);
    d->m_flags = 0xFD;
    Call<void>(0x0045D740, &CBaseComponent::DestroyBody, d);
    Result(0xD740);
    d->m_flags = 0x02;
    Call<void>(0x0045D740, &CBaseComponent::DestroyBody, d);
    Result(0xD741);
    for (unsigned int flags : {0u, 1u, 2u}) {
        d->m_flags = flags == 2 ? 0x3u : 0x0u;
        Result(Rel(Call<CBaseComponent*>(0x0045D780, &CBaseComponent::Destroy, d, flags)));
    }
    d->m_flags = 0xF0;
    for (bool on : {true, true, false, false}) {
        Call<void>(0x0045D760, &CBaseComponent::Suspend, d, on);
        Result(d->m_flags);
    }

    // Destruction: B with three components (deleted first to last), DestroyBody and Destroy(1).
    for (unsigned int flags : {0xFFu, 0u, 1u, 2u}) {
        auto* b = reinterpret_cast<C*>(arena + 0x400);
        std::memset(b, 0xDD, sizeof(C));
        b->m_compositeFlags = 0x10;
        b->mp_components = nullptr;
        for (int i = 0; i < 3; ++i) {
            CBaseComponent* e = MakeComponent(arena, 8 + i, 0x7100 + i, 0, 2);
            e->mp_next = b->mp_components;
            b->mp_components = e;
        }
        if (flags == 0xFF)
            Call<void>(0x0045D890, &C::DestroyBody, b);
        else
            Result(Rel(Call<C*>(0x0045E620, &C::Destroy, b, flags)));
        Result(b->m_compositeFlags);
    }
    return EndRun(arena);
}

// --- components from structures, InitFromStructure --------------------------------------------------------------
std::vector<uint32_t> RunStructureTest(bool original)
{
    uint8_t* arena = BeginRun(original);
    using C = CCompositeObject;
    auto* a = reinterpret_cast<C*>(arena + 0x100);
    a->m_vtable = reinterpret_cast<void**>(g_cobj_objectVtable);
    a->mp_components = MakeComponent(arena, 0, 0x7000, 0, 2);

    // An array of three structures; an empty one.
    auto* array = reinterpret_cast<CArray*>(arena + 0x2400);
    array->m_size = 3;
    auto* elements = reinterpret_cast<uint32_t*>(arena + 0x2410);
    for (int i = 0; i < 3; ++i)
        elements[i] = reinterpret_cast<uint32_t>(Struct(arena, 10 + i));
    Call<void>(0x0045DA90, &C::CreateComponentsFromArray, a, array, Struct(arena, 20));
    array->m_size = 0;
    Call<void>(0x0045DA90, &C::CreateComponentsFromArray, a, array, static_cast<CStruct*>(nullptr));

    // CreateComponentFromStructure: no extra; with extra (a merged copy); no component= (the params' address).
    CStruct* params = Struct(arena, 1);
    Answer('K', params, 0xB6015EA8, {0x7EE1});
    g_cobj_nextType = 0x7EE1;
    Call<void>(0x0045EE20, &C::CreateComponentFromStructure, a, params, static_cast<CStruct*>(nullptr));
    Call<void>(0x0045EE20, &C::CreateComponentFromStructure, a, params, Struct(arena, 2));
    CStruct* bare = Struct(arena, 3);
    g_cobj_nextType = 0x7EE2;
    Call<void>(0x0045EE20, &C::CreateComponentFromStructure, a, bare, static_cast<CStruct*>(nullptr));

    // InitFromStructure: name= and permanent; neither.
    CStruct* init = Struct(arena, 4);
    Answer('K', init, 0xA1DC81F9, {0x1D1D});
    Answer('L', init, 0x23627FD7, {1});
    a->m_objectFlags = 0x100;
    Call<void>(0x0045DAD0, &C::InitFromStructure, a, init);
    Result(a->m_objectFlags);
    a->m_objectFlags = 0x100;
    Call<void>(0x0045DAD0, &C::InitFromStructure, a, Struct(arena, 5));
    Result(a->m_objectFlags);
    return EndRun(arena);
}

// --- the member functions scripts call ------------------------------------------------------------------------
std::vector<uint32_t> RunMemberTest(bool original)
{
    uint8_t* arena = BeginRun(original);
    using C = CCompositeObject;
    auto* a = reinterpret_cast<C*>(arena + 0x100);
    auto* b = reinterpret_cast<C*>(arena + 0x400);
    a->m_vtable = reinterpret_cast<void**>(g_cobj_objectVtable);
    a->m_type = 0x7A7E;
    auto* script = reinterpret_cast<CScript*>(arena + 0x2000);
    script->m_params = Struct(arena, 0);
    CStruct* params = Struct(arena, 1);
    CStruct* empty = Struct(arena, 2);
    // Components of the types 0x7000, 0x7001, `empty`'s address and 0 (what lookups with nothing given find).
    const uint32_t types[4] = {0x7000, 0x7001, reinterpret_cast<uint32_t>(empty), 0};
    for (int i = 0; i < 4; ++i) {
        CBaseComponent* c = MakeComponent(arena, i, types[i], 0, 2);
        c->mp_next = a->mp_components;
        a->mp_components = c;
    }
    a->m_pos = {1.5f, -2.25f, 1000.125f, 0.5f};
    a->m_vel = {3.0f, 4.0f, 12.0f, 1.0f};
    a->m_matrix.row[0] = {0.1f, 0.7f, -0.3f, 0.25f};
    a->m_matrix.row[1] = {-0.6f, 0.2f, 0.9f, 0.125f};
    a->m_matrix.row[2] = {0.33f, -0.44f, 0.55f, 0.0625f};
    a->m_matrix.row[3] = {7.0f, 8.0f, 9.0f, 1.0f};
    b->m_pos = {101.7f, 12.3f, -55.5f, 1.0f};

    Member(0x0045DBC0, &C::Obj_GetPosition, a, params, script);
    Member(0x0045DC50, &C::Obj_GetVelocity, a, params, script);
    Member(0x0045DC80, &C::GetSpeed, a, params, script);
    a->m_vel = {0.1f, -0.2f, 0.3f, 1.0f};
    Member(0x0045DC80, &C::GetSpeed, a, params, script);
    Answer('S', params, 0, {0x1234});
    Member(0x0045D7F0, &C::Obj_PrintDetails, a, params, script);
    Member(0x0045D810, &C::Obj_CreateComponent, a, params, script);
    Answer('V', params, 0xB9D31B0A, {Bits(4.0f), Bits(5.0f), Bits(6.0f), Bits(1.0f)});
    Member(0x0045D830, &C::Obj_SetPosition, a, empty, script);
    Result(Bits(a->m_pos.x));
    Member(0x0045D830, &C::Obj_SetPosition, a, params, script);
    a->m_pos = {1.5f, -2.25f, 1000.125f, 0.5f};
    // Component checks: by name= / unnamed, found and missing.
    Answer('K', params, 0xA1DC81F9, {0x7000});
    Member(0x0045DBF0, &C::Obj_ComponentExists, a, params, script);
    Member(0x0045DBF0, &C::Obj_ComponentExists, a, empty, script);
    Answer('K', params, 0, {0x7001});
    Member(0x0045E410, &C::Obj_HasComponent, a, params, script);
    Answer('K', empty, 0xA1DC81F9, {0x7001});
    Member(0x0045E410, &C::Obj_HasComponent, a, empty, script);
    a->mp_components->m_type = 0x7777; // (no component of type 0 now)
    Member(0x0045E410, &C::Obj_HasComponent, a, empty, script);
    a->mp_components->m_type = 0;
    Member(0x0045DBF0, &C::Obj_ComponentExists, a, empty, script);
    // Flags.
    a->m_compositeFlags = 0x10;
    Member(0x0045DCD0, &C::Obj_Hide, a, params, script);
    Member(0x0045DD70, &C::Obj_IsHidden, a, params, script);
    Member(0x0045DD20, &C::Obj_Unhide, a, params, script);
    Member(0x0045DD70, &C::Obj_IsHidden, a, params, script);
    Member(0x0045DED0, &C::Obj_Suspend, a, params, script);
    Result(a->m_compositeFlags);
    Member(0x0045DF20, &C::Obj_Unsuspend, a, params, script);
    Result(a->m_compositeFlags);
    Member(0x0045E9B0, &C::Obj_Pause, a, params, script);
    Result(a->m_compositeFlags);
    Member(0x0045E9D0, &C::Obj_Unpause, a, params, script);
    Result(a->m_compositeFlags);
    Member(0x0045DC40, &C::Obj_ForceUpdate, a, params, script);
    Member(0x0045E3F0, &C::Obj_GetType, a, params, script);
    // Moving along its axes (y missing).
    CStruct* move = Struct(arena, 3);
    Answer('F', move, 0x7323E97C, {Bits(3.3f)});
    Answer('F', move, 0x9D2D8850, {Bits(-17.77f)});
    Member(0x0045DD80, &C::Obj_MoveLocal, a, move, script);
    Answer('F', move, 0x0424D9EA, {Bits(1e-3f)});
    Member(0x0045DD80, &C::Obj_MoveLocal, a, move, script);
    // (values where rounding the x product to a float first changes the result)
    a->m_pos.x = -7.128984451293945f;
    a->m_matrix.row[0].x = 0.8972200155258179f;
    Answer('F', move, 0x7323E97C, {Bits(-46.11112976074219f)});
    Member(0x0045DD80, &C::Obj_MoveLocal, a, move, script);
    Result(Bits(a->m_pos.x));
    // The distance to an object: found, not found.
    Answer('K', params, 0, {0xB0B});
    g_cobj_found = Rel(b);
    Member(0x0045E0B0, &C::Obj_GetDistanceToObject, a, params, script);
    g_cobj_found = 0;
    Member(0x0045E0B0, &C::Obj_GetDistanceToObject, a, empty, script);
    // The velocity against its at row: backwards, forwards, square (0); the speed against a value.
    for (float z : {-1.0f, 1.0f, 0.0f}) {
        a->m_vel = {0.44f, 0.55f * z, z, 1.0f};
        Member(0x0045E260, &C::Obj_MovingBackward, a, params, script);
    }
    a->m_vel = {0.0f, 0.0f, 0.0f, 1.0f};
    Member(0x0045E260, &C::Obj_MovingBackward, a, params, script);
    a->m_vel = {3.0f, 4.0f, 0.0f, 1.0f};
    CStruct* speed = Struct(arena, 4);
    for (float value : {5.0f, 4.95f, 5.0999f, 5.1f, 4.9f, 4.89f, 7.0f, 3.0f}) {
        Answer('F', speed, 0, {Bits(value)});
        Member(0x0045E2A0, &C::Obj_SpeedNear, a, speed, script);
        Member(0x0045E310, &C::Obj_SpeedAbove, a, speed, script);
        Member(0x0045E380, &C::Obj_SpeedBelow, a, speed, script);
    }
    Member(0x0045E2A0, &C::Obj_SpeedNear, a, empty, script);
    Member(0x0045E310, &C::Obj_SpeedAbove, a, empty, script);
    Member(0x0045E380, &C::Obj_SpeedBelow, a, empty, script);
    // Exactly 0.1 apart (not near); a speed whose sum of squares rounds differently in another order or in floats
    // (16777215^2 + 2 * 0.15^2); speeds that aren't floats, against their rounded values.
    a->m_vel = {0.2f, 0.0f, 0.0f, 1.0f};
    Answer('F', speed, 0, {Bits(0.1f)});
    Member(0x0045E2A0, &C::Obj_SpeedNear, a, speed, script);
    a->m_vel = {0.15f, 0.15f, 16777215.0f, 1.0f};
    Answer('F', speed, 0, {Bits(16777215.0f)});
    Member(0x0045E310, &C::Obj_SpeedAbove, a, speed, script);
    Member(0x0045E380, &C::Obj_SpeedBelow, a, speed, script);
    a->m_vel = {16777215.0f, 0.15f, 0.15f, 1.0f};
    Member(0x0045E310, &C::Obj_SpeedAbove, a, speed, script);
    Member(0x0045E380, &C::Obj_SpeedBelow, a, speed, script);
    Member(0x0045E2A0, &C::Obj_SpeedNear, a, speed, script);
    const float odd[4][3] = {{0.1f, -0.2f, 0.3f}, {1.1f, 2.3f, -0.7f}, {7.7f, 0.01f, 3.3f}, {0.5f, 0.6f, 0.7f}};
    for (const auto& v : odd) {
        a->m_vel = {v[0], v[1], v[2], 1.0f};
        double length = std::sqrt(static_cast<double>(v[0]) * v[0] + static_cast<double>(v[1]) * v[1] +
                                  static_cast<double>(v[2]) * v[2]);
        Answer('F', speed, 0, {Bits(static_cast<float>(length))});
        Member(0x0045E380, &C::Obj_SpeedBelow, a, speed, script);
        Member(0x0045E310, &C::Obj_SpeedAbove, a, speed, script);
    }
    return EndRun(arena);
}

// --- the member functions that work out directions and distances ------------------------------------------
void RecordMatrices(CCompositeObject* o)
{
    for (const Mth::Matrix* m : {&o->m_matrix, &o->m_displayMatrix})
        for (const Mth::Vector& row : m->row)
            Rec(0x3A7, {Bits(row.x), Bits(row.y), Bits(row.z), Bits(row.w)});
}

std::vector<uint32_t> RunMathTest(bool original)
{
    uint8_t* arena = BeginRun(original);
    using C = CCompositeObject;
    auto* script = reinterpret_cast<CScript*>(arena + 0x2000);
    script->m_params = Struct(arena, 0);
    CStruct* params = Struct(arena, 1);
    CStruct* empty = Struct(arena, 2);

    // Objects of the types 0x7A, 0xBAF9B341 (the type 0x0045EF30 looks for) in a stand-in manager's list.
    auto* manager = reinterpret_cast<uint32_t*>(arena + 0x6400);
    manager[0] = reinterpret_cast<uint32_t>(g_cobj_managerVtable);
    auto* head = reinterpret_cast<Mem::ListNode*>(arena + 0x6420);
    g_cobj_listHead = reinterpret_cast<uint32_t>(head);
    C* objects[6];
    const uint32_t types[6] = {0xBAF9B341, 0x7A, 0xBAF9B341, 0xBAF9B341, 0x7A, 0xBAF9B341};
    const float positions[6][3] = {{0.0f, 0.0f, 0.0f},  {3.0f, 0.0f, 4.0f},     {10.0f, -2.0f, 1.0f},
                                   {-7.5f, 1.0f, 2.25f}, {0.5f, 0.5f, 0.5f},    {9.0f, 3.0f, -1.0f}};
    Mem::ListNode* previous = head;
    *head = {reinterpret_cast<void**>(0x006477E4), reinterpret_cast<void*>(-1), 0, nullptr, nullptr};
    for (int i = 0; i < 6; ++i) {
        objects[i] = reinterpret_cast<C*>(arena + 0x100 + 0x120 * i);
        C* o = objects[i];
        o->m_vtable = reinterpret_cast<void**>(g_cobj_objectVtable);
        o->m_type = types[i];
        o->mp_manager = reinterpret_cast<Obj::CBaseManager*>(manager);
        o->m_pos = {positions[i][0], positions[i][1], positions[i][2], 1.0f};
        o->m_node = {reinterpret_cast<void**>(0x006477E4), o, 0, nullptr, previous};
        previous->next = &o->m_node;
        previous = &o->m_node;
    }
    previous->next = head;
    head->prev = previous;
    C* a = objects[0];
    a->m_matrix.row[0] = {0.6f, 0.0f, -0.8f, 0.0f};
    a->m_matrix.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    a->m_matrix.row[2] = {0.8f, 0.0f, 0.6f, 0.0f};
    a->m_matrix.row[3] = {1.0f, 2.0f, 3.0f, 1.0f};
    a->m_displayMatrix = a->m_matrix;
    a->m_displayMatrix.row[2] = {0.3f, -1.7f, 2.2f, 9.0f};

    // GetOrientation: a display at row of some length; of none.
    Member(0x0045DF70, &C::Obj_GetOrientation, a, params, script);
    for (const Mth::Vector& at : {Mth::Vector{0.7f, 0.11f, -0.013f, 0.0f}, Mth::Vector{-123.4f, 5.67f, 0.0089f, 0.0f},
                                  Mth::Vector{1e-3f, 3e-4f, -7e-3f, 0.0f}}) {
        a->m_displayMatrix.row[2] = at;
        Member(0x0045DF70, &C::Obj_GetOrientation, a, params, script);
    }
    a->m_displayMatrix.row[2] = {0.0f, 0.0f, 0.0f, 1.0f};
    Member(0x0045DF70, &C::Obj_GetOrientation, a, params, script);
    // GetDistToNode (the node's position from the stand-in).
    Answer('K', params, 0, {0x0DE0});
    Member(0x0045E020, &C::Obj_GetDistToNode, a, params, script);
    Member(0x0045E020, &C::Obj_GetDistToNode, a, empty, script);
    a->m_pos = {-2.0f, 1.0f, 33.3f, 1.0f};
    Member(0x0045E020, &C::Obj_GetDistToNode, a, params, script);
    a->m_pos = {0.0f, 0.0f, 0.0f, 1.0f};
    // GetOrientationToObject: to objects 1, 2 and none.
    for (uint32_t found : {Rel(objects[1]), Rel(objects[2]), 0u}) {
        g_cobj_found = found;
        Member(0x0045E140, &C::Obj_GetOrientationToObject, a, params, script);
    }
    a->m_matrix.row[0] = {2.5f, 0.3f, -1.1f, 0.0f};
    for (int i = 1; i < 6; ++i) {
        g_cobj_found = Rel(objects[i]);
        Member(0x0045E140, &C::Obj_GetOrientationToObject, a, params, script);
    }
    a->m_matrix.row[0] = {0.6f, 0.0f, -0.8f, 0.0f};
    // FindNearestObjectOfType: from each object, for each type; a type with none; only itself.
    using Nearest = C* (C::*)(uint32_t);
    for (C* o : {objects[0], objects[2], objects[4]}) {
        for (uint32_t type : {0xBAF9B341u, 0x7Au, 0x5555u})
            Result(Rel(Call<C*>(0x0045ED30, static_cast<Nearest>(&C::FindNearestObjectOfType), o, type)));
    }
    objects[5]->m_pos = {1e30f, 1e30f, 1e30f, 1.0f};
    objects[2]->m_pos = {-1e30f, 1e30f, 1e30f, 1.0f};
    objects[3]->m_pos = {1e30f, -1e30f, 1e30f, 1.0f};
    Result(Rel(Call<C*>(0x0045ED30, static_cast<Nearest>(&C::FindNearestObjectOfType), a, 0xBAF9B341u)));
    objects[3]->m_pos = {-7.5f, 1.0f, 2.25f, 1.0f};
    // Two at the same distance (the first one met wins).
    objects[2]->m_pos = {3.0f, 0.0f, 4.0f, 1.0f};
    objects[5]->m_pos = {0.0f, 5.0f, 0.0f, 1.0f};
    objects[3]->m_pos = {0.0f, 0.0f, -5.0f, 1.0f};
    Result(Rel(Call<C*>(0x0045ED30, static_cast<Nearest>(&C::FindNearestObjectOfType), a, 0xBAF9B341u)));
    objects[3]->m_pos = {-7.5f, 1.0f, 2.25f, 1.0f};
    objects[2]->m_pos.w = 0.25f; // (the nearest one below: its w counts)
    // NearestOutsideAngle: the stand-in angle against the parameter; no object of the type.
    CStruct* angle = Struct(arena, 3);
    Answer('F', angle, 0, {Bits(30.0f)});
    for (float result : {0.6f, -0.6f, 0.5235987f, 0.5235988f, -0.52359879f, 0.0f}) {
        g_cobj_angle = result;
        Member(0x0045EF30, &C::Obj_NearestOutsideAngle, a, angle, script);
    }
    for (int i = 0; i < 6; ++i)
        objects[i]->m_type = 0x7A;
    Member(0x0045EF30, &C::Obj_NearestOutsideAngle, a, angle, script);

    // SetOrientation: dir (ordinary; within 0.99 of vertical, both ways; exactly 0.99), quat, angles (some
    // missing); GetQuat (w >= 0, w < 0).
    CStruct* orient = Struct(arena, 4);
    const float dirs[][3] = {{0.3f, 0.2f, -0.9f}, {0.01f, 0.995f, 0.05f}, {0.0f, -2.0f, 0.01f}, {0.1f, 0.99f, 0.0f},
                             {12.7f, -3.3f, 0.123f}, {-0.0071f, 0.43f, 77.7f}, {1e-3f, 0.98999f, 2.1f}};
    for (const auto& d : dirs) {
        Answer('V', orient, 0x455485EF, {Bits(d[0]), Bits(d[1]), Bits(d[2]), Bits(1.0f)});
        Member(0x0045E650, &C::Obj_SetOrientation, a, orient, script);
        RecordMatrices(a);
    }
    CStruct* quat = Struct(arena, 5);
    Answer('V', quat, 0x748CA262, {Bits(0.1f), Bits(-0.5f), Bits(0.3f), Bits(1.0f)});
    a->m_displayMatrix.row[1].y = 77.0f;
    Member(0x0045E650, &C::Obj_SetOrientation, a, quat, script);
    RecordMatrices(a);
    CStruct* euler = Struct(arena, 6);
    Answer('F', euler, 0x7323E97C, {Bits(45.0f)});
    Answer('F', euler, 0x9D2D8850, {Bits(-120.5f)});
    Member(0x0045E650, &C::Obj_SetOrientation, a, euler, script);
    RecordMatrices(a);
    Answer('F', euler, 0x0424D9EA, {Bits(7.25f)});
    Member(0x0045E650, &C::Obj_SetOrientation, a, euler, script);
    for (float w : {0.5f, -0.5f, 0.0f}) {
        g_cobj_quatW = w;
        Member(0x0045E940, &C::Obj_GetQuat, a, params, script);
    }
    return EndRun(arena);
}

// --- the node manager --------------------------------------------------------------------------------------------
// Stand-in arrays: +0x04 the size, the elements from +0x10 (the stand-in GetElement / SetElement use them).
CArray* NodeArray(uint8_t* arena, uint32_t offset, std::initializer_list<uint32_t> elements)
{
    auto* array = reinterpret_cast<CArray*>(arena + offset);
    array->m_size = static_cast<uint32_t>(elements.size());
    uint32_t* p = reinterpret_cast<uint32_t*>(arena + offset + 0x10);
    for (uint32_t e : elements)
        *p++ = e;
    return array;
}

std::vector<uint32_t> RunNodeTest(bool original)
{
    uint8_t* arena = BeginRun(original);
    using M = SkateScript::CNodeManager;
    auto* m = reinterpret_cast<M*>(arena + 0x10000);
    std::memset(m, 0xDD, sizeof(M));
    Result(Rel(Call<M*>(0x0045D370, &M::Construct, m)));

    // Two node arrays: A (3 nodes: the second with position=, the first with links [2, 0]), B (2 nodes).
    CStruct* nodes[5];
    for (int i = 0; i < 5; ++i)
        nodes[i] = Struct(arena, 40 + i);
    CArray* a = NodeArray(arena, 0x2600, {reinterpret_cast<uint32_t>(nodes[0]), reinterpret_cast<uint32_t>(nodes[1]),
                                          reinterpret_cast<uint32_t>(nodes[2])});
    CArray* b = NodeArray(arena, 0x2680, {reinterpret_cast<uint32_t>(nodes[3]), reinterpret_cast<uint32_t>(nodes[4])});
    CArray* links = NodeArray(arena, 0x2700, {2, 0});
    Answer('A', nodes[0], 0x2E7D5EE7, {reinterpret_cast<uint32_t>(links)});
    CArray* noLinks = NodeArray(arena, 0x2780, {});
    Answer('A', nodes[2], 0x2E7D5EE7, {reinterpret_cast<uint32_t>(noLinks)});
    Answer('V', nodes[1], 0xB9D31B0A, {Bits(1.5f), Bits(-2.0f), Bits(3.25f), Bits(1.0f)});
    g_cobj_symbolArrays[0xAAAA] = reinterpret_cast<uint32_t>(a);
    g_cobj_symbolArrays[0xBBBB] = reinterpret_cast<uint32_t>(b);
    using Add = bool (M::*)(uint32_t, bool);
    Result(Call<uint32_t>(0x0045D520, static_cast<Add>(&M::AddNodeArray), m, 0xAAAAu, false) & 0xFF);
    Result(Call<uint32_t>(0x0045D520, static_cast<Add>(&M::AddNodeArray), m, 0xBBBBu, true) & 0xFF);
    Result(Call<uint32_t>(0x0045D520, static_cast<Add>(&M::AddNodeArray), m, 0xAAAAu, true) & 0xFF);
    for (uint32_t name : {0xAAAAu, 0xBBBBu, 0xCCCCu}) {
        Result(Call<uint32_t>(0x0045D270, &M::HasNodeArray, m, name) & 0xFF);
        Result(Call<uint32_t>(0x0045D170, &M::HashNodeIn, m, name, 0x11u, 0x22u) & 0xFF);
    }
    Result(Rel(Call<CArray*>(0x0045D120, &M::GetLastNodeArray, m)));
    Result(Call<uint32_t>(0x0045D150, &M::GetLastNodeArrayName, m));
    Call<void>(0x0045D140, &M::SetOwner, m, static_cast<void*>(arena + 0x2800));
    *reinterpret_cast<uint32_t*>(arena + 0x2858) = 0x5858;
    Result(Call<uint32_t>(0x0045D470, &M::GetOwnerValue58, m));

    // The walk: all five nodes from the start, then past the end; from a hashed name (slots set by hand); ended.
    Call<void>(0x0045D050, &M::StartWalk, m, 0u);
    for (int i = 0; i < 7; ++i)
        Result(Rel(Call<CStruct*>(0x0045D3F0, &M::NextNode, m)));
    const uint32_t name = 0x12340FFF; // (slot 0x3FFF; a collision there wraps to slot 0)
    m->m_slots[0x3FFF] = {1, 0};
    m->m_slots[0] = {0, 2};
    Answer('K', nodes[3], 0xA1DC81F9, {0x99});
    Answer('K', nodes[2], 0xA1DC81F9, {name});
    Call<void>(0x0045D050, &M::StartWalk, m, name);
    Result(m->m_walk.m_array);
    for (int i = 0; i < 3; ++i)
        Result(Rel(Call<CStruct*>(0x0045D3F0, &M::NextNode, m)));
    Call<void>(0x0045D110, &M::EndWalk, m);
    Result(Rel(Call<CStruct*>(0x0045D3F0, &M::NextNode, m)));

    // Removing: the first (the last moves into its place, its slots follow), a missing one, the last.
    g_cobj_symbolArrays[0xCCCC] = reinterpret_cast<uint32_t>(b); // (a's links are names now)
    Result(Call<uint32_t>(0x0045D520, static_cast<Add>(&M::AddNodeArray), m, 0xCCCCu, false) & 0xFF);
    m->m_slots[5] = {1, 1};
    m->m_slots[6] = {0, 1};
    m->m_slots[7] = {2, 0};
    using Remove = bool (M::*)(uint32_t);
    for (uint32_t n : {0xBBBBu, 0xDDDDu, 0xAAAAu, 0xCCCCu}) {
        Result(Call<uint32_t>(0x0045D1C0, static_cast<Remove>(&M::RemoveNodeArray), m, n) & 0xFF);
        Rec(0xD1C0, {m->m_slots[5].m_array, m->m_slots[6].m_array, m->m_slots[7].m_array});
    }
    Result(Call<uint32_t>(0x0045D120, &M::GetLastNodeArray, m));
    Result(Call<uint32_t>(0x0045D150, &M::GetLastNodeArrayName, m));

    // Names from paths; removing by path.
    char* out = reinterpret_cast<char*>(arena + 0x2900);
    const char* paths[] = {"levels\\nj\\nj.qb", "c:/a/b/park.qb", "x\\noext", "dir/a.b.c"};
    for (int i = 0; i < 4; ++i) {
        char* path = reinterpret_cast<char*>(arena + 0x2A00 + 0x40 * i);
        std::memcpy(path, paths[i], std::strlen(paths[i]) + 1);
        std::memset(out, 0xEE, 0x40);
        Result(CallC<uint32_t>(0x0045D2A0, &SkateScript::MakeNodeArrayName, static_cast<const char*>(path), out) &
               0xFF);
        Result(CallC<uint32_t>(0x0045D310, &SkateScript::NodeArrayChecksum, static_cast<const char*>(path)));
    }
    g_cobj_symbolArrays[Crc::GenerateCRCFromString("nj_nodearray")] = reinterpret_cast<uint32_t>(b);
    Result(Call<uint32_t>(0x0045D520, static_cast<Add>(&M::AddNodeArray), m,
                          Crc::GenerateCRCFromString("nj_nodearray"), false) & 0xFF);
    Result(Call<uint32_t>(0x0045D670, &M::RemoveNodeArrayOfFile, m,
                          static_cast<const char*>(reinterpret_cast<char*>(arena + 0x2A00))) & 0xFF);
    Result(m->m_count);

    // The singleton: made, referenced, released twice; a failed allocation; no create.
    GameVariable<uint32_t>(0x006F0D84) = 0;
    GameVariable<uint32_t>(0x006F0D88) = 0;
    for (bool create : {false, true, true}) {
        Result(Rel(CallC<M*>(0x0045D690, &SkateScript::CreateNodeManager, create)));
        Result(GameVariable<uint32_t>(0x006F0D88));
    }
    for (int i = 0; i < 3; ++i) {
        CallC<void>(0x0045D6E0, &SkateScript::ReleaseNodeManager);
        Rec(0xD6E0, {GameVariable<uint32_t>(0x006F0D84), GameVariable<uint32_t>(0x006F0D88)});
    }
    g_cobj_allocFail = true;
    Result(Rel(CallC<M*>(0x0045D690, &SkateScript::CreateNodeManager, true)));
    Result(GameVariable<uint32_t>(0x006F0D88));
    g_cobj_allocFail = false;
    CallC<void>(0x0045D6E0, &SkateScript::ReleaseNodeManager);
    return EndRun(arena);
}

void RunPair(std::vector<uint32_t> (*run)(bool), const char* what)
{
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    Compare(expected, actual, what);
}

void TestCompositeObjects()
{
    SaveGlobals();
    auto* s = new StandIns;
    InstallStandIns(*s);
    RunPair(RunLifeTest, "CCompositeObject: construction, update, hide / suspend, components, member function calls");
    RunPair(RunStructureTest, "CCompositeObject: components from structures, InitFromStructure");
    RunPair(RunMemberTest, "CCompositeObject: the member functions scripts call");
    RunPair(RunMathTest, "CCompositeObject: orientation, distances, the nearest object");
    RunPair(RunNodeTest, "SkateScript::CNodeManager: node arrays, the walk, names from paths, the singleton");
    Cleanup();
}

} // namespace

SELFTEST(Early, TestCompositeObjects, Cleanup);
