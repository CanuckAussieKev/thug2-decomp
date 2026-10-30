// Self-tests for AN-3: the asset types (src/game/ass/asset_types.cpp) - each type's Load (blocking and async, from
// a file and an archive, with and without parameters and a structure, the file missing), LoadFromBuffer (copied or
// not), FinishLoading (the engine's loader working or failing), Unload, FreeBuffer and the others, the original
// and ours on identical stand-in state; every call the engine, file system and scripts get, and the arena, compared.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "game/ass/asset.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/str.h"
#include "game/file/async.h"
#include "game/file/pip.h"
#include "game/gfx/engine_glue.h"
#include "game/obj/cutscene.h"
#include "game/obj/object.h"
#include "game/script/node_manager.h"
#include "game/script/qb.h"
#include "game/script/struct.h"
#include "game/gfx/engine.h"
#include "hook/anim_testkit.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace {

using namespace AnimTests;
using Ass::CAsset;
using Script::CStruct;

struct TypeArena {
    alignas(16) uint8_t heap[0x20000];
    uint8_t asset[0x64];
    uint8_t group[0x64];
    uint8_t files[2][0x40];     // stand-in file handles
    uint8_t manager[0x200];     // the memory manager (+0x110 the top-down heap)
    uint8_t nodeManager[0x20];
    uint8_t params[0x60];       // parameters given to Load
    uint8_t structure[0x20];    // a script structure (its getters are stood in for)
    uint8_t nodes[0x20];        // a script array of nodes
    uint8_t object[0x10];       // an object (vtable +0x00)
    void* objectVtable[8];
    uint8_t symbol[0x20];
    char platform[8];
};
TypeArena g_types;
uint32_t g_typesHeapUsed;
bool g_typesOriginal;

template <typename F>
F TypeFn(uint32_t address)
{
    return reinterpret_cast<F>(Code(g_typesOriginal, address));
}
uint32_t TypesBump(uint32_t size)
{
    if (g_typesHeapUsed + size > sizeof(g_types.heap) - 0x1000)
        g_typesHeapUsed = 0; // (wraps: the test's own blocks only; the last 0x1000 bytes are the group's data)
    uint32_t at = reinterpret_cast<uint32_t>(g_types.heap) + g_typesHeapUsed;
    g_typesHeapUsed += (size + 15) & ~15u;
    std::memset(reinterpret_cast<void*>(at), 0xCD, size);
    return at;
}
uint32_t StringHash(uint32_t text) { return TestStandIns::Hash(reinterpret_cast<const char*>(text)); }

// What the stand-ins answer (set by each case).
int g_openFails;            // AsyncOpen returns null (the first n calls... 1: the first, 2: the second)
int g_opens;
uint32_t g_fileSize;
bool g_loaderWorks;         // the engine's loaders (anim frames, skeleton) say it worked
bool g_hasCas;              // a skinned model with a .cas file
uint32_t g_nodeCount;
bool g_objectFound;
bool g_nodeManager;

// Records {tag, values...} with strings as hashes.
void Rec(uint32_t tag, std::initializer_list<uint32_t> values)
{
    g_trace.push_back(tag);
    g_trace.insert(g_trace.end(), values);
}

// A stand-in at the game's function and ours (when there is one).
void At2(StandIns& s, uint32_t address, const void* ours, uint16_t pop, Handler handler)
{
    if (ours)
        s.At({address, Resolve(ours)}, pop, std::move(handler));
    else
        s.At({address}, pop, std::move(handler));
}
template <typename F>
const void* Ours(F function)
{
    void* p;
    std::memcpy(&p, &function, sizeof(p));
    return p;
}

void StandInEverything(StandIns& s)
{
    // memory
    At2(s, 0x005BAFC0, Ours(&Mem::Manager::Allocate), 12, [](Regs& r) {
        Rec(0xA11C, {r.args[0], r.args[1], r.args[2]});
        return TypesBump(r.args[0]);
    });
    At2(s, 0x005BABA0, Ours(&Mem::Manager::Free), 4, [](Regs& r) {
        Rec(0xF4EE, {r.args[0]});
        return 0u;
    });
    At2(s, 0x005BBAE0, Ours(&Mem::Malloc), 0, [](Regs& r) {
        Rec(0x3A11, {r.args[0]});
        return TypesBump(r.args[0]);
    });
    At2(s, 0x005BBB60, Ours(&Mem::FreeMalloc), 0, [](Regs& r) {
        Rec(0x3F4E, {r.args[0]});
        return 0u;
    });
    At2(s, 0x005BB0E0, Ours(&Mem::Manager::PushContext), 4, [](Regs& r) {
        Rec(0xC7A0, {r.args[0]});
        return 0u;
    });
    At2(s, 0x005BB110, Ours(&Mem::Manager::PopContext), 0, [](Regs&) {
        Rec(0xC7A1, {});
        return 0u;
    });
    // files
    At2(s, 0x005B6C60, Ours(static_cast<File::AsyncFileHandle* (*)(const char*, bool, int)>(&File::AsyncOpen)), 0,
        [](Regs& r) {
            Rec(0x0BE4, {StringHash(r.args[0]), r.args[1] & 0xFF, r.args[2]});
            ++g_opens;
            if (g_openFails == g_opens)
                return 0u;
            return reinterpret_cast<uint32_t>(g_types.files[(g_opens - 1) & 1]);
        });
    At2(s, 0x005B63A0, Ours(&File::AsyncFileHandle::GetFileSize), 0, [](Regs& r) {
        Rec(0x512E, {r.ecx});
        return g_fileSize + (r.ecx == reinterpret_cast<uint32_t>(g_types.files[1]) ? 0x30 : 0);
    });
    At2(s, 0x005B6380, Ours(&File::AsyncFileHandle::SetCallback), 12, [](Regs& r) {
        bool readCallback = r.args[0] == 0x00407710 ||
                            Resolve(reinterpret_cast<void*>(r.args[0])) == AddressOf(&CAsset::ReadCallback);
        Rec(0xCA11, {r.ecx, readCallback, r.args[1], r.args[2]});
        return 0u;
    });
    At2(s, 0x005B6740, Ours(&File::AsyncFileHandle::Read), 12, [](Regs& r) {
        Rec(0x4EAD, {r.ecx, r.args[0], r.args[1], r.args[2]});
        return r.args[2];
    });
    At2(s, 0x005B63D0, Ours(&File::AsyncFileHandle::Cancel), 0, [](Regs& r) {
        Rec(0xCA4C, {r.ecx});
        return 1u;
    });
    At2(s, 0x005B68A0, Ours(&File::AsyncClose), 0, [](Regs& r) {
        Rec(0xC105, {r.args[0]});
        return 1u;
    });
    At2(s, 0x005B7C00, Ours(&Pip::Load), 0, [](Regs& r) {
        Rec(0x919A, {StringHash(r.args[0]), r.args[1], r.args[2], r.args[3], r.args[4]});
        return TypesBump(0x40);
    });
    At2(s, 0x005B72E0, Ours(static_cast<int (*)(const char*)>(&Pip::GetFileSize)), 0, [](Regs& r) {
        Rec(0x9195, {StringHash(r.args[0])});
        return g_fileSize + 0x100;
    });
    At2(s, 0x005B7200, Ours(static_cast<void (*)(uint32_t)>(&Pip::Unload)), 0, [](Regs& r) {
        Rec(0x919F, {r.args[0]});
        return 0u;
    });
    // scripts
    At2(s, 0x00478F60, Ours(static_cast<int (*)(uint32_t)>(&Script::GetInteger)), 0, [](Regs& r) {
        Rec(0x6E70, {r.args[0]});
        return 0u;
    });
    At2(s, 0x00476950, Ours(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum)), 12,
        [](Regs& r) {
            Rec(0x6E50, {r.ecx, r.args[0], r.args[2]});
            *reinterpret_cast<uint32_t*>(r.args[1]) = r.args[0] ^ 0x5A5A5A5A;
            return 1u;
        });
    At2(s, 0x00476630, Ours(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger)), 12,
        [](Regs& r) {
            Rec(0x6E54, {r.ecx, r.args[0], r.args[2]});
            *reinterpret_cast<int*>(r.args[1]) = 77;
            return 1u;
        });
    At2(s, 0x00476B40, Ours(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsFlag)), 4, [](Regs& r) {
        Rec(0x6E5F, {r.ecx, r.args[0]});
        return (r.args[0] >> 2) & 1;
    });
    At2(s, 0x00479090, Ours(static_cast<Script::CArray* (*)(const char*)>(&Script::GetArray)), 0, [](Regs& r) {
        Rec(0x6A44, {StringHash(r.args[0])});
        return reinterpret_cast<uint32_t>(g_types.nodes);
    });
    At2(s, 0x0046CC30, Ours(&Script::CArray::GetElement), 4, [](Regs& r) {
        Rec(0x6E1E, {r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t>(g_types.structure) + r.args[0];
    });
    At2(s, 0x0046F9F0, Ours(&Script::FreeScriptChecksumNames), 0, [](Regs&) {
        Rec(0x6F4E, {});
        return 0u;
    });
    At2(s, 0x0046CC50, Ours(&Script::CArray::SetSizeAndType), 8, [](Regs& r) {
        Rec(0x6551, {r.ecx, r.args[0], r.args[1]});
        reinterpret_cast<Script::CArray*>(r.ecx)->m_size = r.args[0];
        return 0u;
    });
    At2(s, 0x0046CB10, Ours(&Script::CArray::SetElement), 8, [](Regs& r) {
        Rec(0x655E, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    At2(s, 0x00478CC0, Ours(&Script::FindSymbol), 0, [](Regs& r) {
        Rec(0x6F17, {r.args[0]});
        return r.args[0] & 1 ? reinterpret_cast<uint32_t>(g_types.symbol) : 0u;
    });
    At2(s, 0x004711D0, Ours(&Script::CleanUpAndRemoveSymbol), 0, [](Regs& r) {
        Rec(0x6C1E, {r.args[0]});
        return 0u;
    });
    At2(s, 0x00479110, Ours(&Script::CreateSymbol), 0, [](Regs& r) {
        Rec(0x6C4E, {r.args[0]});
        std::memset(g_types.symbol, 0xEE, sizeof(g_types.symbol));
        return reinterpret_cast<uint32_t>(g_types.symbol);
    });
    At2(s, 0x0045D2A0, Ours(&SkateScript::MakeNodeArrayName), 0, [](Regs& r) {
        Rec(0x4A4E, {StringHash(r.args[0])});
        std::snprintf(reinterpret_cast<char*>(r.args[1]), 64, "%s_nodes", reinterpret_cast<const char*>(r.args[0]));
        return 1u;
    });
    At2(s, 0x0045D310, Ours(&SkateScript::NodeArrayChecksum), 0, [](Regs& r) {
        Rec(0x4AC5, {StringHash(r.args[0])});
        return 0x0A55A000u;
    });
    At2(s, 0x0045D520, Ours(&SkateScript::CNodeManager::AddNodeArray), 8, [](Regs& r) {
        Rec(0x4AAD, {r.ecx, r.args[0], r.args[1] & 0xFF});
        return 1u;
    });
    At2(s, 0x0045D1C0, Ours(&SkateScript::CNodeManager::RemoveNodeArray), 4, [](Regs& r) {
        Rec(0x4A4F, {r.ecx, r.args[0]});
        return 1u;
    });
    At2(s, 0x00460AD0, Ours(&Obj::FindObject), 0, [](Regs& r) {
        Rec(0x0F1D, {r.args[0]});
        return g_objectFound && (r.args[0] & 2) ? reinterpret_cast<uint32_t>(g_types.object) : 0u;
    });
    At2(s, 0x004B2320, Ours(&Nx::PlatformExtension), 0, [](Regs&) {
        return reinterpret_cast<uint32_t>(g_types.platform);
    });
    s.At({0x006234FE}, 0, TestStandIns::StandInSprintf); // (the game's sprintf, for the original)
    // the cutscene data
    At2(s, 0x00464640, Ours(&Obj::CCutsceneData::Construct), 0, [](Regs& r) {
        Rec(0xC0C7, {r.ecx});
        *reinterpret_cast<void***>(r.ecx) = g_types.objectVtable; // (deleted through slot 0 by Unload)
        return r.ecx;
    });
    At2(s, 0x004678A0, Ours(&Obj::CCutsceneData::Load), 16, [](Regs& r) {
        Rec(0xC010, {r.ecx, StringHash(r.args[0]), r.args[1], r.args[2] & 0xFF, r.args[3]});
        return static_cast<uint32_t>(g_loaderWorks);
    });
    At2(s, 0x00467B50, Ours(&Obj::CCutsceneData::IsLoaded), 0, [](Regs& r) {
        Rec(0xC015, {r.ecx});
        return 1u;
    });
    // the engine's loaders
    struct Loader {
        uint32_t address;
        uint16_t pop;
        int args;
        bool returnsThis;
    };
    const Loader loaders[] = {
        {0x00482AF0, 0, 0, true},  {0x00482B30, 0, 0, false}, {0x004856C0, 12, 3, false}, {0x004A2C20, 0, 0, true},
        {0x004A39C0, 12, 3, false}, {0x005A5990, 0, 4, false}, {0x005A59B0, 0, 1, false}, {0x0048CD80, 0, 7, false},
        {0x0048C690, 0, 2, false},  {0x0049D590, 8, 2, false}, {0x0049C9F0, 4, 1, false}, {0x0049CFB0, 0, 0, false},
        {0x0048C030, 0, 8, false},  {0x0048C080, 0, 2, false}, {0x00490B80, 4, 1, false}, {0x00490A90, 0, 0, false},
        {0x0049F050, 0, 7, false},  {0x0049EFD0, 0, 1, false},
    };
    for (const Loader& l : loaders) {
        s.At({l.address}, l.pop, [l](Regs& r) {
            g_trace.push_back(l.address);
            bool thiscall = l.pop || l.returnsThis || l.address == 0x00482B30 || l.address == 0x0049CFB0 ||
                            l.address == 0x00490A90;
            g_trace.push_back(thiscall ? r.ecx : 0); // (ECX means nothing to a cdecl function)
            if (l.address == 0x004A2C20)
                *reinterpret_cast<void***>(r.ecx) = g_types.objectVtable; // (the skeleton: deleted through slot 0)
            for (int i = 0; i < l.args; ++i)
                g_trace.push_back(l.address == 0x0048CD80 && i == 0 ? StringHash(r.args[0]) : r.args[i]);
            if (l.returnsThis)
                return r.ecx;
            if (l.address == 0x004856C0 || l.address == 0x004A39C0)
                return static_cast<uint32_t>(g_loaderWorks);
            return 0xE0000000u + (l.address & 0xFFFFF);
        });
    }
    // file names the engine makes
    for (uint32_t address : {0x0048C390u, 0x0049CA20u, 0x00490AD0u, 0x0049EF90u}) {
        s.At({address}, 0, [address](Regs& r) {
            Rec(address, {StringHash(r.args[1]), address == 0x0048C390 || address == 0x0049CA20 ? r.args[2] & 0xFF : 0});
            std::snprintf(reinterpret_cast<char*>(r.args[0]), 64, "file_%X_%s", address & 0xFFF,
                          reinterpret_cast<const char*>(r.args[1]));
            return 0u;
        });
    }
    s.At({0x0048BFE0, AddressOf(Nx::Engine::SplitSkinName)}, 0, [](Regs& r) {
        Rec(0x8BFE, {StringHash(r.args[0])});
        std::snprintf(reinterpret_cast<char*>(r.args[1]), 64, "Base_%s", reinterpret_cast<const char*>(r.args[0]));
        return static_cast<uint32_t>(g_hasCas);
    });
}

// One type's functions, by address (0: it has none of its own).
struct TypeInfo {
    uint32_t construct, load, loadFromBuffer, finish, unload, freeBuffer, getName, getType;
};
const TypeInfo kTypes[] = {
    {0x004071A0, 0x00407300, 0x004071C0, 0x004074F0, 0x004074A0, 0x00407290, 0x004072D0, 0x004072E0}, // animation
    {0x00407A30, 0x00408AD0, 0, 0, 0x00408A40, 0, 0x00408AB0, 0x00408AC0},                           // cutscene
    {0x00408BB0, 0x00408D10, 0x00408EA0, 0x00408C10, 0x00408F00, 0x00407290, 0x00408BD0, 0x00408BE0}, // node array
    {0x00409220, 0x004093F0, 0x00409520, 0x00409240, 0x00409380, 0, 0x004093D0, 0},                   // scene
    {0x004095F0, 0x00409760, 0x00409890, 0x00409610, 0x00409700, 0, 0x00409750, 0},                   // scene collision
    {0x00409970, 0x00409A50, 0x00409990, 0x00409B80, 0x004099E0, 0x00409A10, 0x00409A30, 0x00409A40}, // skeleton
    {0x00409C40, 0x00409EB0, 0x0040A140, 0x00409C70, 0x00409D30, 0x00409DA0, 0x00409DF0, 0x00409E00}, // skinned model
    {0x0040A210, 0x0040A2A0, 0x0040A400, 0x0040A510, 0x0040A230, 0, 0x0040A280, 0x0040A290},          // skin collision
    {0x0040A580, 0x0040A720, 0x0040A840, 0x0040A5A0, 0x0040A6C0, 0, 0x0040A710, 0},                   // tex dict
};

CAsset* TheAsset() { return reinterpret_cast<CAsset*>(g_types.asset); }
void Snapshot()
{
    const auto* words = reinterpret_cast<const uint32_t*>(g_types.asset);
    for (int i = 0; i < 0x64 / 4; ++i)
        g_trace.push_back(words[i]);
    g_trace.push_back(GameVariable<uint32_t>(0x006918D8));
    const auto* pool = reinterpret_cast<const uint32_t*>(0x00691758); // (the load parameters)
    g_trace.insert(g_trace.end(), pool, pool + 0x180 / 4);
}
// A fresh asset of the type, its group a base asset with data.
void Fresh(const TypeInfo& t)
{
    std::memset(g_types.asset, 0, sizeof(g_types.asset)); // (zeroed, as the manager's Calloc leaves it)
    TypeFn<CAsset*(__thiscall*)(CAsset*)>(t.construct)(TheAsset());
    TheAsset()->m_checksum = 0xC0DE0000u + (t.construct & 0xFFFF);
    TheAsset()->m_group = reinterpret_cast<CAsset*>(g_types.group);
    GameVariable<uint32_t>(0x006918D8) = 0;
    std::memset(reinterpret_cast<void*>(0x00691758), 0xEE, 0x180); // (each case from a dirty pool)
    g_opens = 0;
}

void TestAnimAssetTypes()
{
    GameMemory memory({{0x007D1800, 4}, {0x007CC979, 1}, {0x006F0D84, 4}, {0x006918D8, 4}, {0x00691758, 0x180},
                       {0x006F22B4, 0x20}, {0x006F3614, 0x20}});
    StandIns& s = *new StandIns();
    StandInEverything(s);
    void* objectSlots[8];
    for (int i = 0; i < 8; ++i) {
        objectSlots[i] = s.Thunk(0, [i](Regs& r) {
            Rec(0x0B50u + i, {r.ecx});
            return 0u;
        });
    }
    void* deleteSlot = s.Thunk(4, [](Regs& r) {
        Rec(0x0DE1, {r.ecx, r.args[0]});
        return r.ecx;
    });
    auto runOnce = [&](bool original) {
        g_typesOriginal = original;
        g_trace.clear();
        std::memset(&g_types, 0, sizeof(g_types));
        std::memset(g_types.files, 0xFF, sizeof(g_types.files));
        std::memset(reinterpret_cast<void*>(0x00691758), 0xEE, 0x180);
        g_typesHeapUsed = 0;
        std::memcpy(g_types.platform, "pc", 3);
        GameVariable<void*>(0x007D1800) = g_types.manager;
        *reinterpret_cast<uint32_t*>(g_types.manager + 0x110) = 0x70D0110u;
        GameVariable<bool>(0x007CC979) = true;
        std::memset(reinterpret_cast<void*>(0x006F22B4), 0, 0x20);
        std::memset(reinterpret_cast<void*>(0x006F3614), 0, 0x20);
        for (int i = 0; i < 8; ++i)
            g_types.objectVtable[i] = objectSlots[i];
        g_types.objectVtable[0] = deleteSlot; // (the scalar deleting destructor: one argument)
        *reinterpret_cast<void***>(g_types.object) = g_types.objectVtable;
        reinterpret_cast<Script::CArray*>(g_types.nodes)->m_size = 3;
        for (int i = 0; i < 0x60; ++i)
            g_types.params[i] = static_cast<uint8_t>(i * 7 + 1);
        std::memcpy(g_types.params + 0x10, "parm", 5); // (a name for those that keep one)
        std::memset(g_types.group, 0, sizeof(g_types.group));
        TypeFn<CAsset*(__thiscall*)(CAsset*)>(0x00407910)(reinterpret_cast<CAsset*>(g_types.group));
        reinterpret_cast<CAsset*>(g_types.group)->m_data = g_types.heap + 0x1F000; // the group's data
        *reinterpret_cast<uint32_t*>(g_types.heap + 0x1F000 + 0x8C) = 0xC011D474;
        if (!original)
            UseOurVtables();

        using LoadFn = int(__thiscall*)(CAsset*, const char*, bool, bool, void*, void*);
        using BufferFn = int(__thiscall*)(CAsset*, void*, uint32_t, bool, void*, void*);
        for (const TypeInfo& t : kTypes) {
            g_trace.push_back(0x7E000000u + t.construct);
            if (t.getName)
                g_trace.push_back(StringHash(reinterpret_cast<uint32_t>(TypeFn<const char*(__thiscall*)(CAsset*)>(t.getName)(TheAsset()))));
            // Load: blocking / async x file / archive x parameters / structure, and the file missing.
            for (int c = 0; c < 10; ++c) {
                Fresh(t);
                g_fileSize = 0x200 + 0x10 * c;
                g_loaderWorks = c != 3;
                g_hasCas = (c & 1) != 0;
                g_openFails = c == 8 ? 1 : c == 9 ? 2 : 0;
                bool async = (c & 2) != 0;
                bool archive = (c & 4) != 0;
                void* params = c % 3 == 0 ? g_types.params : nullptr;
                void* structure = c % 3 == 1 ? g_types.structure : nullptr;
                g_trace.push_back(0x10AD0000u + c);
                int result = TypeFn<LoadFn>(t.load)(TheAsset(), c == 5 ? "Level\\Some.Name" : "skater", async, archive,
                                                     params, structure);
                g_trace.push_back(static_cast<uint32_t>(result));
                Snapshot();
                if (result == 0 && t.unload) {
                    g_nodeCount = 3;
                    g_objectFound = c != 2;
                    if (t.construct == 0x00408BB0 && async) // (not finished: no nodes yet)
                        reinterpret_cast<uint32_t*>(TheAsset())[0x58 / 4] = reinterpret_cast<uint32_t>(g_types.nodes);
                    GameVariable<void*>(0x006F0D84) = c == 4 ? nullptr : g_types.nodeManager;
                    g_trace.push_back(static_cast<uint32_t>(TypeFn<int(__thiscall*)(CAsset*)>(t.unload)(TheAsset())));
                    Snapshot();
                }
                GameVariable<void*>(0x006F0D84) = g_types.nodeManager;
            }
            // LoadFromBuffer: copied or not, parameters / structure.
            if (t.loadFromBuffer) {
                for (int c = 0; c < 4; ++c) {
                    Fresh(t);
                    g_loaderWorks = c != 2;
                    g_trace.push_back(0xB0F00000u + c);
                    g_trace.push_back(static_cast<uint32_t>(TypeFn<BufferFn>(t.loadFromBuffer)(
                        TheAsset(), g_types.params, 0x24 + c, c & 1, c == 1 || c == 2 ? g_types.params : nullptr,
                        c == 3 ? g_types.structure : nullptr)));
                    Snapshot();
                }
            }
            // FinishLoading on its own (the loader failing and working), then Unload with a buffer still there.
            if (t.finish) {
                for (int c = 0; c < 2; ++c) {
                    Fresh(t);
                    g_loaderWorks = c == 1;
                    if (t.construct == 0x00408BB0 || t.construct == 0x00409220 || t.construct == 0x004095F0 ||
                        t.construct == 0x00409C40 || t.construct == 0x0040A210 || t.construct == 0x0040A580) {
                        // (the types that keep parameters: Load leaves them, here from the pool)
                        void* p = TypeFn<void*(__cdecl*)(uint32_t)>(0x00407730)(0x60);
                        std::memcpy(p, g_types.params, 0x50);
                        reinterpret_cast<void**>(TheAsset())[t.construct == 0x00408BB0 ? 0x5C / 4
                                                             : t.construct == 0x00409C40 ? 0x50 / 4
                                                                                          : 0x4C / 4] = p;
                    }
                    TheAsset()->m_file = reinterpret_cast<File::AsyncFileHandle*>(g_types.files[0]);
                    g_trace.push_back(0xF1000000u + c);
                    g_trace.push_back(TypeFn<bool(__thiscall*)(CAsset*, void*, uint32_t)>(t.finish)(TheAsset(),
                                                                                               g_types.params, 0x40));
                    Snapshot();
                    if (t.construct == 0x00408BB0)
                        reinterpret_cast<uint32_t*>(TheAsset())[0x58 / 4] = reinterpret_cast<uint32_t>(g_types.nodes);
                    TheAsset()->m_buffer = g_types.heap + 0x1E000;
                    g_trace.push_back(static_cast<uint32_t>(TypeFn<int(__thiscall*)(CAsset*)>(t.unload)(TheAsset())));
                    Snapshot();
                }
            }
            // FreeBuffer: an archive's, a buffer, nothing.
            if (t.freeBuffer) {
                for (int c = 0; c < 3; ++c) {
                    Fresh(t);
                    auto* words = reinterpret_cast<uint32_t*>(TheAsset());
                    words[0x24 / 4] = c < 2 ? 0xB0FF0000u + c : 0;
                    words[0x4C / 4] = c == 0 ? 0xA4C00000u : 0;
                    words[0x58 / 4] = c == 1 ? 0xCA5B0000u : 0;
                    TypeFn<void(__thiscall*)(CAsset*)>(t.freeBuffer)(TheAsset());
                    Snapshot();
                }
            }
            if (t.getType)
                g_trace.push_back(static_cast<uint32_t>(TypeFn<int(__thiscall*)(CAsset*)>(t.getType)(TheAsset())));
        }
        // The node array's type when owned; its CreateNodeArray (an old symbol and none).
        Fresh(kTypes[2]);
        g_types.asset[0x60] = 1;
        g_trace.push_back(static_cast<uint32_t>(TypeFn<int(__thiscall*)(CAsset*)>(0x00408BE0)(TheAsset())));
        for (uint32_t name : {0x1234u, 0x1235u}) {
            g_trace.push_back(reinterpret_cast<uint32_t>(
                TypeFn<void*(__thiscall*)(CAsset*, uint32_t, int)>(0x004090D0)(TheAsset(), name, 2 + (name & 1))));
            Snapshot();
            for (int k = 0; k < 8; ++k)
                g_trace.push_back(reinterpret_cast<uint32_t*>(g_types.symbol)[k]);
        }
        // The skinned model's abort (with and without the .cas file), SetRead (two files), the parameters' Clear.
        Fresh(kTypes[6]);
        auto* skin = reinterpret_cast<uint32_t*>(TheAsset());
        skin[0x54 / 4] = reinterpret_cast<uint32_t>(g_types.files[1]);
        skin[0x20 / 4] = reinterpret_cast<uint32_t>(g_types.files[0]);
        g_trace.push_back(TypeFn<bool(__thiscall*)(CAsset*)>(0x00409D70)(TheAsset()));
        Snapshot();
        g_trace.push_back(TypeFn<bool(__thiscall*)(CAsset*)>(0x00409D70)(TheAsset()));
        skin[0x5C / 4] = 2;
        for (int k = 0; k < 2; ++k) {
            g_trace.push_back(TypeFn<bool(__thiscall*)(CAsset*)>(0x00409C60)(TheAsset()));
            Snapshot();
        }
        std::memset(g_types.params, 0x99, 0x14);
        g_trace.push_back(reinterpret_cast<uint32_t>(TypeFn<void*(__thiscall*)(void*)>(0x00409E90)(g_types.params)));
        for (int k = 0; k < 5; ++k)
            g_trace.push_back(reinterpret_cast<uint32_t*>(g_types.params)[k]);
        // Reload (the animation's): unloaded, loaded again.
        Fresh(kTypes[0]);
        g_openFails = 0;
        g_trace.push_back(TypeFn<bool(__thiscall*)(CAsset*, const char*)>(0x00408A70)(TheAsset(), "again"));
        g_openFails = 1;
        g_opens = 0;
        g_trace.push_back(TypeFn<bool(__thiscall*)(CAsset*, const char*)>(0x00408A70)(TheAsset(), "missing"));
        Snapshot();
        // The parameter pool: all four taken, a fifth refused, one given back, an unknown one.
        GameVariable<uint32_t>(0x006918D8) = 0;
        void* blocks[5];
        for (void*& b : blocks)
            b = TypeFn<void*(__cdecl*)(uint32_t)>(0x00407730)(0x40);
        for (void* b : blocks)
            g_trace.push_back(reinterpret_cast<uint32_t>(b));
        TypeFn<void(__cdecl*)(void*)>(0x00407770)(blocks[2]);
        TypeFn<void(__cdecl*)(void*)>(0x00407770)(g_types.params);
        g_trace.push_back(GameVariable<uint32_t>(0x006918D8));
        g_trace.push_back(reinterpret_cast<uint32_t>(TypeFn<void*(__cdecl*)(uint32_t)>(0x00407730)(0)));

        EndRunPatches();
        std::vector<uint32_t> trace = g_trace;
        const auto* words = reinterpret_cast<const uint32_t*>(&g_types);
        trace.insert(trace.end(), words, words + sizeof(g_types) / 4);
        const auto* pool = reinterpret_cast<const uint32_t*>(0x00691758);
        trace.insert(trace.end(), pool, pool + 0x180 / 4);
        return trace;
    };
    std::vector<uint32_t> expected = runOnce(true);
    std::vector<uint32_t> actual = runOnce(false);
    delete &s;
    CompareTraces(expected, actual, "Ass asset types (load / from buffer / finish / unload / free / abort)");
}
SELFTEST(Early, TestAnimAssetTypes, TestStandIns::RemoveStandIns);

} // namespace
