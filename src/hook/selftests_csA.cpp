// Self-tests for CScript's life cycle (src/game/script/cscript_core.cpp): the originals against ours on a stand-in
// memory manager, CScript pools, symbol table and stand-in objects; the script cache, Interrupt (group C), the
// event handler table, the tracker and CRefCounted's constructor / destructor are recording stand-ins.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <new>
#include <vector>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/obj/smtptr.h"
#include "game/obj/tracker.h"
#include "game/script/cache.h"
#include "game/obj/event.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using Script::CScript;
using Script::CStruct;
using Script::CSymbolTableEntry;
using Script::SStructScript;

constexpr uint32_t kArenaSize = 0x40000;
alignas(16) uint8_t g_csA_arenas[2][kArenaSize];
CSymbolTableEntry* g_csA_table[0x1000];
std::vector<Saved> g_csA_saved;
bool g_csA_swapped = false;

void RestoreCsAGlobals()
{
    RemoveStandIns();
    if (!g_csA_swapped)
        return;
    RestoreAll(g_csA_saved);
    g_csA_swapped = false;
}

// Names (none inside the arenas' address range).
constexpr uint32_t kNameA = 0x0A0A0001, kNameB = 0x0B0B0002, kNameC = 0x0C0C0003, kMissing = 0x0D0D0004;
constexpr uint32_t kFrame0 = 0x0F0F0010, kFrame2 = 0x0F0F0012, kUndefined = 0x0E0E0005;
constexpr uint32_t kX = 0x01000011, kY = 0x01000022, kZ = 0x01000033, kW = 0x01000044, kFlag = 0x01000055;
constexpr uint32_t kCall = 0x01000066, kQ = 0x01000077, kR = 0x01000088;
constexpr uint32_t kExitTop = 0x0E000001, kExit0 = 0x0E000010, kExit1 = 0x0E000011, kExit2 = 0x0E000012;
const char* const kStringName = "csA_test_script";

struct Code {
    std::vector<uint8_t> b;
    Code& t(uint8_t v)
    {
        b.push_back(v);
        return *this;
    }
    Code& u(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            b.push_back(static_cast<uint8_t>(v >> (8 * i)));
        return *this;
    }
};

using ThisVoid = void(__thiscall*)(CScript*);

void TestCScriptCore()
{
    using Mem::Heap;
    using Mem::Manager;
    using Mem::Region;
    if (GameVariable<void*>(0x007D1800)) {
        Log("self-test: TestCScriptCore skipped (the memory manager is already set up)");
        return;
    }
    g_csA_saved = {{0x007CC979, 1, {}},  {0x006F2338, 16, {}}, {0x006F3614, 16, {}}, {0x006F22B4, 16, {}},
                   {0x006F3644, 16, {}}, {0x006F3654, 16, {}}, {0x006F362C, 16, {}}, {0x006F3570, 24, {}},
                   {0x006F3624, 4, {}},  {0x006F3538, 4, {}},  {0x006F353C, 4, {}},  {0x006F3540, 4, {}},
                   {0x006F3548, 16, {}}, {0x006F3558, 4, {}},  {0x006F2358, 4, {}},  {0x006F0FC0, 4, {}},
                   {0x007D1800, 4, {}},  {0x006F2578, 16, {}}};
    SaveAll(g_csA_saved);
    g_csA_swapped = true;

    auto run = [&](bool original) {
        g_trace.clear();
        for (Saved& s : g_csA_saved)
            std::memset(reinterpret_cast<void*>(s.address), 0, s.size);
        GameVariable<bool>(0x007CC979) = true;
        std::memset(g_csA_table, 0, sizeof(g_csA_table));
        GameVariable<CSymbolTableEntry**>(0x006F3624) = g_csA_table;

        uint8_t* arena = g_csA_arenas[original ? 0 : 1];
        std::memset(arena, 0, kArenaSize);
        std::memset(arena + 0x1000, 0xCD, kArenaSize - 0x1000);
        auto* manager = reinterpret_cast<Manager*>(arena);
        GameVariable<void*>(0x007D1800) = manager;
        // A script heap and the current (default) heap.
        auto* region = new (arena + 0x600) Region(arena + 0x1000, arena + 0x20000);
        auto* heap = new (arena + 0x620) Heap(region, 1, "script");
        auto* defaultRegion = new (arena + 0x6C0) Region(arena + 0x20000, arena + kArenaSize);
        auto* defaultHeap = new (arena + 0x6E0) Heap(defaultRegion, 1, "default");
        manager->m_heaps[0] = heap;
        manager->m_heaps[1] = defaultHeap;
        manager->m_heapCount = 2;
        manager->m_scriptHeap = heap;
        manager->PushContext(defaultHeap);
        auto base = reinterpret_cast<uint32_t>(arena);
        auto relocate = [&](uint32_t word) {
            return word >= base && word <= base + kArenaSize ? 0xA0000000u + (word - base) : word;
        };

        // Stand-in objects (CRefCounted: a vtable and the list of smart pointers), the event handler tables, the
        // cache and tracker pointers, the stored randoms (owner +0x00, next +0x58).
        auto* objA = reinterpret_cast<Obj::CObject*>(arena + 0x780);
        auto* objB = reinterpret_cast<Obj::CObject*>(arena + 0x790);
        reinterpret_cast<uint32_t*>(objA)[0] = 0x11110001;
        reinterpret_cast<uint32_t*>(objB)[0] = 0x11110002;
        auto* busyTable = reinterpret_cast<Obj::CEventHandlerTable*>(arena + 0x800);
        reinterpret_cast<uint32_t*>(busyTable)[2] = 0x01010101;
        reinterpret_cast<uint32_t*>(busyTable)[3] = 2; // (being run)
        auto* otherTable = reinterpret_cast<Obj::CEventHandlerTable*>(arena + 0x820);
        reinterpret_cast<uint32_t*>(otherTable)[3] = 1;
        GameVariable<void*>(0x006F3558) = arena + 0x7E0;
        GameVariable<void*>(0x006F0FC0) = arena + 0x7F0;
        GameVariable<uint32_t>(0x006F3540) = 0x100;
        GameVariable<int>(0x006F353C) = 5;
        auto* randoms = reinterpret_cast<uint32_t*>(arena + 0x880);
        auto* randoms2 = reinterpret_cast<uint32_t*>(arena + 0x8E0);
        auto* randoms3 = reinterpret_cast<uint32_t*>(arena + 0x940);
        randoms[0x58 / 4] = reinterpret_cast<uint32_t>(randoms2);
        randoms2[0x58 / 4] = reinterpret_cast<uint32_t>(randoms3);
        GameVariable<uint32_t*>(0x006F2358) = randoms;

        // The scripts' code (in the arena).
        auto place = [&](uint32_t offset, const Code& code) {
            std::memcpy(arena + offset, code.b.data(), code.b.size());
            return arena + offset;
        };
        const uint8_t* codeA =
            place(0xA00, Code().t(0x16).u(kX).t(0x07).t(0x17).u(1).t(0x16).u(kFlag).t(0x01).t(0x16).u(kCall).t(0x01).t(0x24));
        const uint8_t* codeB = place(0xA40, Code().t(0x16).u(kY).t(0x07).t(0x17).u(7).t(0x01).t(0x24));
        const uint8_t* codeC = place(0xA80, Code()
                                                .t(0x16).u(kZ).t(0x07).t(0x17).u(9).t(0x01)
                                                .t(0x16).u(kCall).t(0x01)
                                                .t(0x24).t(0x02).u(0x0000002A).t(0x99));

        // Symbols (the .qb file in unknown8).
        auto symbol = [&](uint32_t name, uint32_t file) {
            CSymbolTableEntry* entry = Script::CreateSymbol(name);
            entry->unknown8 = file;
            entry->type = Script::ESYMBOLTYPE_QSCRIPT;
        };
        symbol(kNameA, 0xF11E000A);
        symbol(kNameB, 0xF11E000B);
        symbol(kNameC, 0xF11E000C);
        symbol(kFrame0, 0xF11E0010);
        const uint32_t stringName = Crc::GenerateCRCFromString(kStringName);

        auto* s = new StandIns;
        s->At({0x00475750, AddressOf(&Script::CScript::Interrupt)}, 8, [](Regs& r) { // -> Interrupt (group C)
            auto* script = reinterpret_cast<CScript*>(r.ecx);
            Record({0x5750, r.ecx, r.args[0], r.args[1], reinterpret_cast<uint32_t>(script->m_pc),
                    reinterpret_cast<uint32_t>(script->m_codeCopy), static_cast<uint32_t>(script->m_numReturnAddresses),
                    script->m_onExitScript, reinterpret_cast<uint32_t>(script->m_params)});
            return 0u;
        });
        s->At({0x00475D60, AddressOf(&Script::CScriptCache::ReleaseScript)}, 4, [](Regs& r) {
            Record({0x5D60, r.ecx, r.args[0]});
            return 0u;
        });
        s->At({0x00475E80, AddressOf(&Script::CScriptCache::GetScript)}, 4, [=](Regs& r) {
            Record({0x5E80, r.ecx, r.args[0]});
            uint32_t name = r.args[0];
            const uint8_t* code = name == kNameA ? codeA : name == kNameB || name == stringName ? codeB
                                                         : name == kFrame0                     ? codeC
                                                                                               : nullptr;
            return reinterpret_cast<uint32_t>(code);
        });
        s->At({0x0045FA60, AddressOf(&Obj::CEventHandlerTable::UnregisterWithTracker)}, 4, [](Regs& r) {
            Record({0xFA60, r.ecx, r.args[0]});
            return 0u;
        });
        s->At({0x0045FE60, AddressOf(static_cast<void (Obj::CEventHandlerTable::*)(uint32_t)>(&Obj::CEventHandlerTable::RemoveGroup))}, 4, [](Regs& r) {
            Record({0xFE60, r.ecx, r.args[0]});
            return 0u;
        });
        s->At({0x0045FB70, AddressOf(&Obj::CEventHandlerTable::Destroy)}, 0, [](Regs& r) {
            Record({0xFB70, r.ecx, reinterpret_cast<uint32_t*>(r.ecx)[3]});
            return 0u;
        });
        s->At({0x00462B90, AddressOf(&Obj::CTracker::ForgetScript)}, 4, [](Regs& r) {
            Record({0x2B90, r.ecx, r.args[0]});
            return 0u;
        });
        s->At({0x004637B0, AddressOf(&Obj::CRefCounted::Construct)}, 0, [](Regs& r) {
            auto* object = reinterpret_cast<uint32_t*>(r.ecx);
            Record({0x37B0, r.ecx, object[0], object[1]});
            object[0] = 0x006478E4;
            object[1] = 0;
            return r.ecx;
        });
        s->At({0x00463870, AddressOf(&Obj::CRefCounted::Destruct)}, 0, [](Regs& r) {
            auto* object = reinterpret_cast<uint32_t*>(r.ecx);
            Record({0x3870, r.ecx, object[0], object[1]});
            return 0u;
        });

        // Every live script's whole state and the objects' smart pointer lists, after each call.
        std::vector<CScript*> live;
        auto snap = [&]() {
            for (CScript* script : live) {
                auto* words = reinterpret_cast<const uint32_t*>(script);
                g_trace.insert(g_trace.end(), words, words + sizeof(CScript) / 4);
            }
            auto* a = reinterpret_cast<const uint32_t*>(objA);
            auto* b = reinterpret_cast<const uint32_t*>(objB);
            Record({0x0B1EC7, a[1], b[1]});
        };

        // The functions under test, original or ours.
        auto newScript = [&]() {
            return original ? Original<void*(__cdecl*)(size_t)>(0x00449970)(sizeof(CScript))
                            : CScript::operator new(sizeof(CScript));
        };
        auto deleteScript = [&](void* p) {
            if (original)
                Original<void(__cdecl*)(void*)>(0x004499B0)(p);
            else
                CScript::operator delete(p);
        };
        auto construct = [&](void* p) {
            auto* script = static_cast<CScript*>(p);
            return original ? Original<CScript*(__thiscall*)(CScript*)>(0x004738E0)(script) : script->Construct();
        };
        auto call = [&](uint32_t address, CScript* script, void (CScript::*ours)()) {
            if (original)
                Original<ThisVoid>(address)(script);
            else
                (script->*ours)();
            snap();
        };
        auto copyIfOwned = [&](CScript* script, CStruct* params) {
            CStruct* r = original ? Original<CStruct*(__thiscall*)(CScript*, CStruct*)>(0x004739D0)(script, params)
                                  : script->CopyParamsIfOwned(params);
            Record({0x39D0, reinterpret_cast<uint32_t>(r)});
            snap();
        };
        auto setScript = [&](CScript* script, uint32_t name, const uint8_t* code, CStruct* params, Obj::CObject* object) {
            if (original)
                Original<void(__thiscall*)(CScript*, uint32_t, const uint8_t*, CStruct*, Obj::CObject*)>(0x00474540)(
                    script, name, code, params, object);
            else
                script->SetScript(name, code, params, object);
            snap();
        };
        auto setScriptByName = [&](CScript* script, uint32_t name, CStruct* params, Obj::CObject* object) {
            if (original)
                Original<void(__thiscall*)(CScript*, uint32_t, CStruct*, Obj::CObject*)>(0x004747A0)(script, name,
                                                                                                     params, object);
            else
                script->SetScript(name, params, object);
            snap();
        };
        auto setScriptByString = [&](CScript* script, const char* name, CStruct* params, Obj::CObject* object) {
            if (original)
                Original<void(__thiscall*)(CScript*, const char*, CStruct*, Obj::CObject*)>(0x004747C0)(script, name,
                                                                                                        params, object);
            else
                script->SetScript(name, params, object);
            snap();
        };
        auto setScriptCopy = [&](CScript* script, SStructScript* code, CStruct* params, Obj::CObject* object) {
            if (original)
                Original<void(__thiscall*)(CScript*, SStructScript*, CStruct*, Obj::CObject*)>(0x00474720)(
                    script, code, params, object);
            else
                script->SetScriptCopy(code, params, object);
            snap();
        };
        auto stillDefined = [&](CScript* script) {
            bool r = original ? Original<bool(__thiscall*)(CScript*)>(0x00472C90)(script) : script->AllScriptsStillDefined();
            Record({0x2C90, r ? 1u : 0u});
        };
        auto gotScript = [&](CScript* script) {
            bool r = original ? Original<bool(__thiscall*)(CScript*)>(0x00472940)(script) : script->GotScript();
            Record({0x2940, r ? 1u : 0u});
        };
        auto setSpawnerFile = [&](CScript* script, CScript* spawner) {
            if (original)
                Original<void(__thiscall*)(CScript*, CScript*)>(0x00472F90)(script, spawner);
            else
                script->SetSpawnerFile(spawner);
            snap();
        };
        auto objectDestroyed = [&](CScript* script, Obj::CObject* object) {
            if (original)
                Original<void(__thiscall*)(CScript*, Obj::CObject*)>(0x00473BE0)(script, object);
            else
                script->ObjectDestroyed(object);
            snap();
        };
        auto setName = [&](uint32_t address, CScript* script, uint32_t name, void (CScript::*ours)(uint32_t)) {
            if (original)
                Original<void(__thiscall*)(CScript*, uint32_t)>(address)(script, name);
            else
                (script->*ours)(name);
            snap();
        };
        auto findId = [&](uint32_t id) {
            CScript* r = original ? Original<CScript*(__cdecl*)(uint32_t)>(0x004734B0)(id) : Script::FindScriptWithUniqueId(id);
            Record({0x34B0, reinterpret_cast<uint32_t>(r)});
        };
        auto newStruct = [&](uint32_t name, int value) {
            CStruct* structure = Script::NewStruct()->Init();
            if (name)
                structure->AddInteger(name, value);
            return structure;
        };
        auto globals = [&]() {
            Record({0x6F3538, GameVariable<uint32_t>(0x006F3538), GameVariable<uint32_t>(0x006F353C),
                    GameVariable<uint32_t>(0x006F3540)});
        };

        // The CScript pools: pool 1 (2 scripts) current, then none (the heap), then pool 0 (1 script).
        auto& pools = GameVariable<Mem::PoolPair>(0x006F3548);
        auto* pool0 = new (arena + 0xC00) Mem::Pool(sizeof(CScript), 1, "CScript0");
        auto* pool1 = new (arena + 0xC80) Mem::Pool(sizeof(CScript), 2, "CScript1");
        pools.pools[1] = pool1;
        pools.current = 1;
        CScript* s1 = construct(newScript());
        CScript* s2 = construct(newScript());
        pools.current = 0;
        CScript* s3 = construct(newScript());
        pools.pools[0] = pool0;
        CScript* s4 = construct(newScript());
        Record({reinterpret_cast<uint32_t>(s1), reinterpret_cast<uint32_t>(s2), reinterpret_cast<uint32_t>(s3),
                reinterpret_cast<uint32_t>(s4)});
        live = {s1, s2, s3, s4};
        globals();
        randoms[0] = reinterpret_cast<uint32_t>(s1);
        randoms2[0] = reinterpret_cast<uint32_t>(s3);
        randoms3[0] = reinterpret_cast<uint32_t>(s1);

        // CopyParamsIfOwned with no parameters at all.
        CStruct* outside = newStruct(kY, 2);
        copyIfOwned(s1, nullptr);
        copyIfOwned(s1, outside);

        // SetScript from the cache (defaults + `outside`), then the copies of owned parameters.
        setScript(s1, kNameA, nullptr, outside, objA);
        copyIfOwned(s1, outside);
        copyIfOwned(s1, s1->m_params);
        CStruct* q = newStruct(kQ, 3);
        s1->m_functionParams->AddStructurePointer(kQ, q);
        CStruct* r = newStruct(kR, 4);
        s1->m_params->AddStructurePointer(kR, r);
        copyIfOwned(s1, q);
        copyIfOwned(s1, r);
        copyIfOwned(s1, s1->m_functionParams);
        // Owned parameters (freed by the ClearScript inside), another object.
        setScript(s1, kNameB, nullptr, s1->m_params, objB);
        setScriptByName(s1, kNameA, outside, nullptr);
        setScriptByString(s2, kStringName, outside, objA);
        // No such script: stopped, reported (with and without an event handler table / tracker flag).
        s3->m_eventHandlers = otherTable;
        s3->m_spawnFlags |= 0x10 | 0x04;
        setScript(s3, kMissing, nullptr, outside, objA);
        setScript(s2, kMissing, nullptr, s2->m_params, objB);
        setScriptByString(s2, kStringName, nullptr, objA);
        // A private copy of the code (it ends with a line number).
        SStructScript copied = {kNameC, codeC};
        setScriptCopy(s4, &copied, outside, objB);
        Record({reinterpret_cast<uint32_t>(s4->m_codeCopy), reinterpret_cast<uint32_t>(s4->m_pc)});
        // Code given (not from the cache).
        setScript(s3, kUndefined, codeB, nullptr, objA);

        // s4 gets a call stack of 3 (on the script heap) and 10 loops.
        auto* frames = static_cast<Script::SReturnAddress*>(Mem::Malloc(16 * sizeof(Script::SReturnAddress)));
        std::memset(frames, 0, 16 * sizeof(Script::SReturnAddress));
        frames[0] = {kUndefined, newStruct(kW, 4), codeA, objA, nullptr, kExit0, nullptr, 0.5f, 3, 4, 5, 1, {}};
        frames[1] = {kNameA, nullptr, codeB, objB, nullptr, 0, nullptr, 0, 0, 0, 0, 2, {}};
        frames[2] = {kFrame2, newStruct(kX, 6), codeA, objA, nullptr, kExit2, nullptr, 0, 0, 0, 0, 0, {}};
        s4->m_returnAddresses = frames;
        s4->m_numReturnAddresses = 3;
        s4->m_loops = static_cast<Script::SLoop*>(Mem::Malloc(10 * sizeof(Script::SLoop)));
        s4->m_currentLoop = s4->m_loops + 1;
        s4->m_onExitScript = kExitTop;
        s4->m_waitType = 7;
        s4->m_waitCounter = 8;
        s4->m_waitArg = 9;
        s4->m_waitComponent = reinterpret_cast<Obj::CBaseComponent*>(arena + 0x7B0);
        s4->m_flags = 0xFF;

        // Still defined: every level (the bottom one skipped with a private copy), or just the top.
        stillDefined(s4); // frame 2 undefined
        frames[2].mScriptName = kNameB;
        stillDefined(s4); // frame 0 undefined, skipped
        uint8_t* codeCopy = s4->m_codeCopy;
        s4->m_codeCopy = nullptr;
        stillDefined(s4); // frame 0 undefined
        frames[0].mScriptName = kFrame0;
        stillDefined(s4); // all defined
        frames[1].mScriptName = kUndefined;
        stillDefined(s4); // frame 1 undefined
        frames[1].mScriptName = kNameA;
        s4->m_codeCopy = codeCopy;
        uint32_t top = s4->m_scriptName;
        s4->m_scriptName = kUndefined;
        stillDefined(s4); // the top undefined
        s4->m_scriptName = top;
        stillDefined(s1); // no call stack, defined
        stillDefined(s3); // no call stack, undefined
        s3->m_codeCopy = codeCopy;
        stillDefined(s3); // ... with a private copy
        s3->m_codeCopy = nullptr;
        gotScript(s1);
        gotScript(s4);
        gotScript(s2);
        uint32_t s1Name = s1->m_scriptName;
        s1->m_scriptName = 0;
        gotScript(s1); // (code but no name)
        s1->m_scriptName = s1Name;

        // The spawner's file: its bottom level's script.
        setSpawnerFile(s1, s4);
        setSpawnerFile(s2, s1);
        Record({s1->m_spawnerFile, s2->m_spawnerFile});

        // On-exit scripts: every level's cleared (s1 with two inline levels for the time being).
        s1->m_onExitScript = kExitTop;
        s1->m_inlineReturnAddresses[0].mOnExitScript = kExit0;
        s1->m_inlineReturnAddresses[1].mOnExitScript = kExit1;
        s1->m_numReturnAddresses = 2;
        s1->m_returnAddresses[1].mpObject = objA;
        objectDestroyed(s1, objA); // (only a level's: s1 itself has no object)
        call(0x00472F00, s1, &CScript::ClearOnExitScripts);
        Record({s1->m_onExitScript, s1->m_inlineReturnAddresses[0].mOnExitScript,
                s1->m_inlineReturnAddresses[1].mOnExitScript});
        s1->m_numReturnAddresses = 0;
        s1->m_onExitScript = kExit1;
        setName(0x00472ED0, s1, 0x0E0E0E0E, &CScript::SetOnExceptionScript);
        setName(0x00472EF0, s2, kExit2, &CScript::SetOnExitScript);
        uint32_t onException = original ? Original<uint32_t(__thiscall*)(CScript*)>(0x00472EE0)(s1)
                                        : s1->GetOnExceptionScript();
        Record({onException});

        // Objects going: none, an object of some levels, the script's own object.
        objectDestroyed(s4, nullptr);
        Record({s4->m_waitType});
        objectDestroyed(s4, objA);
        Record({s4->m_waitType});
        s4->m_waitType = 6;
        objectDestroyed(s4, objB);

        // Unique ids.
        findId(s1->m_uniqueId);
        findId(s4->m_uniqueId);
        findId(s3->m_uniqueId);
        findId(0xDEAD);

        // Restart: from the bottom level of a call stack (the on-exit scripts run, every level released), and
        // a script with none.
        frames[0].mpObject = objB; // (the script's own object is gone: objectDestroyed above)
        s4->m_waitType = 3;
        call(0x004747F0, s4, &CScript::Restart);
        call(0x004747F0, s1, &CScript::Restart);
        call(0x00473880, s2, &CScript::RunOnExitScripts);
        s2->m_onExitScript = kExit2;
        call(0x00473010, s3, &CScript::ReportStopped);
        s3->m_waitType = 4;
        s3->m_waitCounter = 2;
        call(0x00473A80, s3, &CScript::ClearScript);

        // Destroyed: the middle of the list, the head, the tail, the last; event handler tables being run or
        // not, callback parameters, the tracker flag.
        s1->m_eventHandlers = busyTable;
        s1->m_callbackParams = newStruct(kZ, 5);
        auto* table = static_cast<uint32_t*>(Mem::New(0x10));
        table[0] = 0;
        table[1] = 0;
        table[2] = 0x01;
        table[3] = 0;
        s2->m_eventHandlers = reinterpret_cast<Obj::CEventHandlerTable*>(table);
        s3->m_eventHandlers = nullptr;
        call(0x00474420, s2, &CScript::Destroy);
        globals();
        call(0x00474420, s4, &CScript::Destroy);
        globals();
        call(0x00474420, s1, &CScript::Destroy);
        globals();
        call(0x00474420, s3, &CScript::Destroy);
        globals();
        // Deleted: the current pool's, the other pool's (the current one missing, then there), the heap's.
        deleteScript(s4);
        pools.pools[0] = nullptr;
        deleteScript(s2);
        pools.pools[0] = pool0;
        deleteScript(s1);
        deleteScript(s3);
        Record({static_cast<uint32_t>(pool0->m_used), static_cast<uint32_t>(pool1->m_used), reinterpret_cast<uint32_t>(pool0->m_freeList),
                reinterpret_cast<uint32_t>(pool1->m_freeList)});
        RemoveStandIns();

        std::vector<uint32_t> trace = g_trace;
        for (uint32_t& word : trace)
            word = relocate(word);
        std::vector<uint32_t> image(kArenaSize / 4);
        std::memcpy(image.data(), arena, kArenaSize);
        for (uint32_t& word : image) {
            word = relocate(word);
            uint32_t high = word >> 16;
            if (high >= (base >> 16) && high <= ((base + kArenaSize) >> 16))
                word = (word & 0xFFFF) | ((0xA000u + high - (base >> 16)) << 16);
        }
        trace.insert(trace.end(), image.begin(), image.end());
        GameVariable<void*>(0x007D1800) = nullptr;
        return trace;
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    RestoreCsAGlobals();
    Compare(expected, actual,
            "CScript life: new/delete, Construct, SetScript (all kinds), CopyParamsIfOwned, ClearScript, Restart, "
            "AllScriptsStillDefined, ObjectDestroyed, on-exit scripts, Destroy");
}

} // namespace

SELFTEST(Early, TestCScriptCore, RestoreCsAGlobals);
