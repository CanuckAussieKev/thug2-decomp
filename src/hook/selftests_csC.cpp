// Self-tests for CScript group C (src/game/script/cscript_wait.cpp): waits, the call stack, interrupts, event
// handlers. The original and ours run the same sequence on identical stand-in scripts in an arena; everything they
// call outside the group (SetScript, Update, the script cache, ParseArguments, the structure functions, the memory
// manager, the smart pointers, the clock, the frame time, the tracker, the event handler table) is stood in for by
// recording code (written over the game's function and ours alike), so the two are compared call by call and
// then the whole arena word by word.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/obj/smtptr.h"
#include "game/obj/tracker.h"
#include "game/script/cache.h"
#include "game/obj/event.h"
#include "game/script/cscript.h"
#include "game/script/parse.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using Script::CScript;
using Script::SReturnAddress;

constexpr uint32_t kArenaSize = 0x10000;
// (aligned to 64K, and both mapped in either run - ArenaWord / ArenaWordOrHigh, selftest.h: a value that only
// happens to fall inside one of them maps the same way in each)
ArenaPair<kArenaSize> g_csc_storage;
uint8_t (*const g_csc_arenas)[kArenaSize] = g_csc_storage.Get();
uint32_t g_csc_base;      // the current run's arena
uint32_t g_csc_bump;      // the stand-in allocator's next block
bool g_csc_allocFail;     // the stand-in allocator returns null
int g_csc_clock;          // the stand-in game clock
uint32_t g_csc_update;    // what the stand-in Update returns
double g_csc_frame;       // what GetSlomoFrameSeconds returns (read by the stand-in code)
std::vector<Saved> g_csc_saved;

uint32_t Rel(uint32_t word) { return ArenaWord(g_csc_arenas, kArenaSize, word); }
uint32_t Rel(const void* p) { return Rel(reinterpret_cast<uint32_t>(p)); }
void Rec(uint32_t tag, std::initializer_list<uint32_t> values)
{
    g_trace.push_back(tag);
    for (uint32_t v : values)
        g_trace.push_back(Rel(v));
}
uint32_t Bump(uint32_t size, uint8_t fill)
{
    uint32_t block = g_csc_bump;
    g_csc_bump += (size + 15) & ~15u;
    std::memset(reinterpret_cast<void*>(block), fill, size);
    return block;
}

void Cleanup()
{
    RemoveStandIns();
    RestoreAll(g_csc_saved);
    g_csc_saved.clear();
}

void InstallStandIns(StandIns& s)
{
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Rec(0xA11, {r.args[0], r.args[1], r.args[2]});
        return g_csc_allocFail ? 0u : Bump(r.args[0], 0xCD);
    });
    s.At({0x005BBAE0, AddressOf(&Mem::Malloc)}, 0, [](Regs& r) {
        Rec(0xA12, {r.args[0]});
        return Bump(r.args[0], 0xCD);
    });
    s.At({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, [](Regs& r) {
        Rec(0xA13, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, [](Regs& r) {
        Rec(0xA14, {r.ecx});
        return 0u;
    });
    s.At({0x00476410, AddressOf(&Script::CStruct::Init)}, 0, [](Regs& r) {
        Rec(0x5701, {r.ecx});
        return r.ecx;
    });
    s.At({0x00477130, AddressOf(&Script::CStruct::Clear)}, 0, [](Regs& r) {
        Rec(0x5702, {r.ecx});
        return 0u;
    });
    s.At({0x00409070, AddressOf(&Script::DeleteStruct)}, 0, [](Regs& r) {
        Rec(0x5703, {r.args[0]});
        return 0u;
    });
    s.At({0x00478480, AddressOf(&Script::CStruct::AppendStructure)}, 4, [](Regs& r) {
        Rec(0x5704, {r.ecx, r.args[0]});
        return r.ecx;
    });
    s.At({0x00478540, AddressOf(&Script::CStruct::AppendStructureIfAny)}, 4, [](Regs& r) {
        Rec(0x5705, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00476490, AddressOf(&Script::CStruct::GetNextComponent)}, 4, [](Regs& r) {
        Rec(0x5706, {r.ecx, r.args[0]});
        return *reinterpret_cast<uint32_t*>(r.ecx); // (the stand-in structure: its first component)
    });
    using ParseFn = const uint8_t* (*)(Script::CStruct*, const uint8_t*, Script::CStruct*);
    s.At({0x00470F30, AddressOf(static_cast<ParseFn>(&Script::ParseArguments))}, 0, [](Regs& r) {
        Rec(0x5707, {r.args[0], r.args[1], r.args[2]});
        return r.args[1] + 7;
    });
    s.At({0x004637C0, AddressOf(&Obj::CRefCounted::AddSmartPointer)}, 4, [](Regs& r) {
        Rec(0x5300, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x004637E0, AddressOf(&Obj::CRefCounted::RemoveSmartPointer)}, 4, [](Regs& r) {
        Rec(0x5301, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00475E80, AddressOf(&Script::CScriptCache::GetScript)}, 4, [](Regs& r) {
        Rec(0xCA01, {r.ecx, r.args[0]});
        return g_csc_base + 0x7000 + (r.args[0] & 0xFF0);
    });
    s.At({0x00475D60, AddressOf(&Script::CScriptCache::ReleaseScript)}, 4, [](Regs& r) {
        Rec(0xCA02, {r.ecx, r.args[0]});
        return 0u;
    });
    // SetScript (group A): enough of it to see what happens after (the name and the code).
    s.At({0x00474540, AddressOf(static_cast<void (Script::CScript::*)(uint32_t, const uint8_t*, Script::CStruct*, Obj::CObject*)>(&Script::CScript::SetScript))}, 16, [](Regs& r) {
        Rec(0x5E7, {r.ecx, r.args[0], r.args[1], r.args[2], r.args[3]});
        auto* script = reinterpret_cast<CScript*>(r.ecx);
        script->m_scriptName = r.args[0];
        script->m_pc = reinterpret_cast<const uint8_t*>(r.args[1] ? r.args[1] : g_csc_base + 0x7F00);
        return 0u;
    });
    s.At({0x004750B0, AddressOf(&Script::CScript::Update)}, 0, [](Regs& r) {
        Rec(0x0BDA, {r.ecx});
        return g_csc_update;
    });
    s.At({0x0044ECE0}, 0, [](Regs&) {
        Rec(0xC10C, {});
        return static_cast<uint32_t>(g_csc_clock);
    });
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        Rec(0xDB6, {r.args[0], r.args[1]});
        return 0xFFFFFFFFu;
    });
    // The frame time: FLD qword [g_csc_frame]; RET (x87, as both return it).
    for (uint32_t address : {0x005BEAA0u, AddressOf(&Tmr::GetSlomoFrameSeconds)}) {
        std::vector<uint8_t> code = {0xDD, 0x05};
        std::vector<uint8_t> where = Dword(reinterpret_cast<uint32_t>(&g_csc_frame));
        code.insert(code.end(), where.begin(), where.end());
        code.push_back(0xC3);
        s.Bytes(address, code);
    }
    // The tracker.
    s.At({0x00463370, AddressOf(&Obj::CTracker::AddListener)}, 8, [](Regs& r) {
        Rec(0x7A01, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00463450, AddressOf(&Obj::CTracker::RemoveListener)}, 8, [](Regs& r) {
        Rec(0x7A02, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    // The event handler table.
    s.At({0x0045F820, AddressOf(&Obj::CEventHandlerTable::Construct)}, 0, [](Regs& r) {
        Rec(0xE820, {r.ecx});
        return r.ecx;
    });
    const std::pair<uint32_t, uint32_t> oneArgument[] = {
        {0x0045FA60u, AddressOf(&Obj::CEventHandlerTable::UnregisterWithTracker)},
        {0x0045FA10u, AddressOf(&Obj::CEventHandlerTable::RegisterWithTracker)},
        {0x0045FDF0u, AddressOf(&Obj::CEventHandlerTable::RemoveType)},
        {0x0045F840u, AddressOf(&Obj::CEventHandlerTable::MarkGroup)},
        {0x0045FED0u, AddressOf(&Obj::CEventHandlerTable::RemoveMarked)},
    };
    for (auto [address, ours] : oneArgument) {
        s.At({address, ours}, 4, [address](Regs& r) {
            Rec(address, {r.ecx, r.args[0]});
            return 0u;
        });
    }
    s.At({0x004601A0, AddressOf(&Obj::CEventHandlerTable::AddHandlers)}, 8, [](Regs& r) {
        Rec(0xE1A0, {r.ecx, r.args[0], r.args[1] & 0xFF});
        return 0u;
    });
    s.At({0x0045FF50, AddressOf(&Obj::CEventHandlerTable::Compact)}, 0, [](Regs& r) {
        Rec(0xEF50, {r.ecx});
        return 0u;
    });
    s.At({0x0045FBC0, AddressOf(&Obj::CEventHandlerTable::AddHandler)}, 0x18, [](Regs& r) {
        Rec(0xEBC0, {r.ecx, r.args[0], r.args[1], r.args[2], r.args[3], r.args[4] & 0xFF, r.args[5]});
        return 0u;
    });
    s.At({0x004600E0, AddressOf(static_cast<void (Obj::CEventHandlerTable::*)(Script::CScript*, uint32_t)>(&Obj::CEventHandlerTable::RemoveGroup))}, 8, [](Regs& r) {
        Rec(0xE0E0, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00460470, AddressOf(&Obj::CEventHandlerTable::PassEvent)}, 8, [](Regs& r) {
        Rec(0xE470, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x0045F880, AddressOf(&Obj::CEventHandlerTable::PrintTable)}, 0, [](Regs& r) {
        Rec(0xE880, {r.args[0]});
        return 0x4321u + (r.args[0] & 0xF);
    });
}

// A component a script can wait on: vtable +0x18 asks it (it ends the wait when the argument is 7).
void* g_csc_componentVtable[8];
void* g_csc_componentCheck;

uint32_t CallKeepingEcx(void* code, CScript* script, void* component)
{
    uint32_t after;
    __asm {
        mov ecx, script
        push component
        call code
        mov after, ecx
    }
    return after;
}

void TestCScriptWait()
{
    g_csc_saved = {
        {0x006F3538, 4, {}},  // the script list
        {0x006F352C, 1, {}},  // one-per-frame released
        {0x00696D60, 4, {}},  // the clock offset
        {0x006F0FC0, 4, {}},  // the tracker
        {0x006F3558, 4, {}},  // the script cache
        {0x007D1800, 4, {}},  // the memory manager
        {0x007CC979, 1, {}},  // the memory manager is ready
        {0x006F3614, 16, {}}, // the CStruct pools
    };
    SaveAll(g_csc_saved);
    auto* s = new StandIns;
    InstallStandIns(*s);
    g_csc_componentCheck = s->Thunk(8, [](Regs& r) {
        Rec(0xC0C0, {r.ecx, r.args[0], r.args[1]});
        if (r.args[1] == 7)
            reinterpret_cast<CScript*>(r.args[0])->m_waitType = 0;
        return 0u;
    });
    for (void*& slot : g_csc_componentVtable)
        slot = nullptr;
    g_csc_componentVtable[6] = g_csc_componentCheck;

    auto run = [&](bool original) -> std::vector<uint32_t> {
        g_trace.clear();
        uint8_t* arena = g_csc_arenas[original ? 0 : 1];
        std::memset(arena, 0, kArenaSize);
        g_csc_base = reinterpret_cast<uint32_t>(arena);
        g_csc_bump = g_csc_base + 0x8000;
        g_csc_allocFail = false;
        g_csc_clock = 0;
        g_csc_update = 2;
        g_csc_frame = 1.0 / 60.0;
        for (Saved& saved : g_csc_saved)
            std::memset(reinterpret_cast<void*>(saved.address), 0, saved.size);
        GameVariable<bool>(0x007CC979) = true;
        auto* manager = reinterpret_cast<Mem::Manager*>(arena + 0x6000);
        manager->m_scriptHeap = reinterpret_cast<Mem::Heap*>(0x5C1F0000);
        manager->m_frontEndHeap = reinterpret_cast<Mem::Heap*>(0xF0E70000);
        GameVariable<void*>(0x007D1800) = manager;
        GameVariable<uint32_t>(0x006F0FC0) = 0x7AC4E400;
        GameVariable<uint32_t>(0x006F3558) = 0xCAC4E000;
        GameVariable<int>(0x00696D60) = 50;

        // Scripts: 0x100 bytes apart from +0x100.
        auto script = [&](int i) {
            auto* p = reinterpret_cast<CScript*>(arena + 0x100 + 0x100 * i);
            return p;
        };
        for (int i = 0; i < 12; ++i) {
            CScript* p = script(i);
            p->m_returnAddresses = p->m_inlineReturnAddresses;
            p->m_loops = &p->m_inlineLoop;
            p->m_uniqueId = 100 + i;
            p->m_scriptName = 0x5C000000u + i;
        }
        auto* components = reinterpret_cast<uint32_t*>(arena + 0x1000); // 4 components of 0x20 bytes
        auto* vtable = reinterpret_cast<void**>(arena + 0x1F00);         // (in the arena: compared relocated)
        std::memcpy(vtable, g_csc_componentVtable, sizeof(g_csc_componentVtable));
        for (int i = 0; i < 4; ++i) {
            components[i * 8] = reinterpret_cast<uint32_t>(vtable);
            components[i * 8 + 2] = 0x100u << i; // its flags
        }
        auto component = [&](int i) { return reinterpret_cast<Obj::CBaseComponent*>(components + i * 8); };
        auto object = [&](int i) { return reinterpret_cast<Obj::CObject*>(arena + 0x1100 + 0x20 * i); };
        auto structure = [&](int i) { return reinterpret_cast<Script::CStruct*>(arena + 0x1200 + 0x10 * i); };
        auto code = [&](int i) { return reinterpret_cast<const uint8_t*>(arena + 0x7000 + 0x40 * i); };

        auto state = [&](CScript* p) {
            uint32_t timer;
            std::memcpy(&timer, &p->m_waitTimer, 4);
            Rec(0x57A7E, {p->m_waitType, static_cast<uint32_t>(p->m_waitCounter), timer, p->m_waitTimerStarted,
                          p->m_waitArg, GameVariable<uint8_t>(0x006F352C)});
        };
        using Fn0 = void(__thiscall*)(CScript*);
        using FnI = void(__thiscall*)(CScript*, int);
        auto updateWait = [&](CScript* p) {
            if (original)
                Original<Fn0>(0x00473050)(p);
            else
                p->UpdateWait();
            state(p);
        };
        auto wait = [&](uint32_t address, CScript* p, int value) {
            if (original)
                Original<FnI>(address)(p, value);
            else if (address == 0x00472BD0)
                p->Wait(value);
            else if (address == 0x00472BF0)
                p->WaitOnePerFrame(value);
            else if (address == 0x00472C10)
                p->WaitTime(value);
            else if (address == 0x00473310)
                p->WaitRealTime(value);
            else if (address == 0x00472FD0)
                p->Block(static_cast<uint32_t>(value));
            state(p);
            g_trace.push_back(p->m_spawnFlags);
        };
        auto setTimer = [&](CScript* p, uint32_t bits) { std::memcpy(&p->m_waitTimer, &bits, 4); };

        // --- counter: 3 more Updates, then done.
        CScript* a = script(0);
        CScript* b = script(1);
        wait(0x00472BD0, a, 3);
        for (int i = 0; i < 5; ++i)
            updateWait(a);
        wait(0x00472BD0, a, 0);
        updateWait(a);
        // --- one per frame: both count down; at 0 the first goes on, the other waits for the next frame.
        wait(0x00472BF0, a, 1);
        wait(0x00472BF0, b, 0);
        updateWait(a);
        updateWait(b); // (b first to 0: released)
        updateWait(a); // waits on at 0
        updateWait(a);
        GameVariable<uint8_t>(0x006F352C) = 0;
        updateWait(a);
        updateWait(a); // (done: nothing)
        // --- timer: 50 ms of 1/60 s frames.
        wait(0x00472C10, a, 50);
        for (int i = 0; i < 5; ++i)
            updateWait(a);
        // shorter than a frame: the first Update zeroes it, the second ends it.
        wait(0x00472C10, a, 5);
        updateWait(a);
        updateWait(a);
        updateWait(a);
        // exactly a frame left (subtracted to 0), then done.
        g_csc_frame = 0.015625;
        wait(0x00472C10, a, 1000);
        setTimer(a, 0x3C800000); // 0.015625f
        updateWait(a);
        updateWait(a);
        // the same after a first Update: subtracted (not ended)
        wait(0x00472C10, a, 1000);
        setTimer(a, 0x3C800000);
        a->m_waitTimerStarted = true;
        updateWait(a);
        updateWait(a);
        // unsigned milliseconds, odd values
        wait(0x00472C10, a, -1);
        wait(0x00472C10, a, 0x7FFFFFFF);
        wait(0x00472C10, a, 123457);
        updateWait(a);
        // a NaN frame time: subtracted
        g_csc_frame = std::numeric_limits<double>::quiet_NaN();
        wait(0x00472C10, a, 40);
        updateWait(a);
        updateWait(a);
        g_csc_frame = 0.0333;
        wait(0x00472C10, a, 30);
        a->m_waitTimerStarted = true;
        updateWait(a);
        // --- real timer: from clock + offset.
        g_csc_clock = 1000;
        wait(0x00473310, a, 100);
        g_csc_clock = 1099;
        updateWait(a);
        g_csc_clock = 1100;
        updateWait(a);
        g_csc_clock = 5;
        wait(0x00473310, a, -16); // (4294967280 ms)
        g_csc_clock = -12;
        updateWait(a);
        g_csc_clock = -11;
        updateWait(a);
        wait(0x00473310, a, 10);
        setTimer(a, 0x7FC00000); // NaN: never
        g_csc_clock = 100000;
        updateWait(a);
        // the elapsed time as unsigned: a start "in the future" is a long time ago
        wait(0x00473310, a, 1000000);
        a->m_waitArg = 0x7FFFFFFF;
        g_csc_clock = 0;
        updateWait(a);
        // --- components (types 5-10): asked; argument 7 ends the wait.
        using SetWaitFn = void(__thiscall*)(CScript*, uint32_t, Obj::CBaseComponent*, uint32_t);
        for (uint32_t type = 5; type <= 10; ++type) {
            if (original)
                Original<SetWaitFn>(0x004739A0)(a, type, component(type & 3), type == 8 ? 7 : type);
            else
                a->SetWaitOnComponent(static_cast<Script::EWaitType>(type), component(type & 3), type == 8 ? 7 : type);
            state(a);
            g_trace.push_back(Rel(a->m_waitComponent));
            updateWait(a);
        }
        // --- blocked, none and unknown types: nothing.
        for (uint32_t type : {3u, 0u, 12u, 0x13u, 0xFFFFFFFFu}) {
            a->m_waitType = type;
            a->m_waitCounter = 5;
            updateWait(a);
        }
        // --- StopWaiting, IsBlocked, Block.
        for (uint32_t type : {3u, 2u, 0x103u}) {
            for (bool print : {false, true}) {
                a->m_waitType = type;
                bool blocked = original ? Original<bool(__thiscall*)(CScript*, bool)>(0x00472900)(a, print)
                                        : a->IsBlocked(print);
                g_trace.push_back(blocked);
            }
        }
        a->m_waitType = 4;
        if (original)
            Original<Fn0>(0x004728F0)(a);
        else
            a->StopWaiting();
        state(a);
        a->m_spawnFlags = 0x41;
        wait(0x00472FD0, a, 0);
        wait(0x00472FD0, a, 0x1234ABCD);
        b->m_spawnFlags = 0x0E;
        wait(0x00472FD0, b, 0x55);

        // --- ComponentDestroyed (through the stub: ECX kept) and on every script.
        auto frame = [&](CScript* p, int i) -> SReturnAddress& { return p->m_returnAddresses[i]; };
        CScript* c = script(2);
        c->m_numReturnAddresses = 2;
        c->m_waitType = 6;
        c->m_waitComponent = component(1);
        frame(c, 0).mpWaitComponent = component(1);
        frame(c, 0).mWaitType = 0xABC5;
        frame(c, 1).mpWaitComponent = component(2);
        frame(c, 1).mWaitType = 0x1237;
        CScript* d = script(3);
        d->m_waitType = 7;
        d->m_waitComponent = nullptr;
        d->m_numReturnAddresses = 1;
        frame(d, 0).mpWaitComponent = nullptr;
        frame(d, 0).mWaitType = 0x46;
        void* destroyed = original ? reinterpret_cast<void*>(0x00472D20) : reinterpret_cast<void*>(&Script::ComponentDestroyedStub);
        for (int i : {3, 1, 2}) {
            for (CScript* p : {c, d}) {
                uint32_t ecx = CallKeepingEcx(destroyed, p, i == 3 ? nullptr : component(i));
                g_trace.push_back(ecx == reinterpret_cast<uint32_t>(p));
                g_trace.push_back(p->m_waitType);
                g_trace.push_back(frame(p, 0).mWaitType);
                g_trace.push_back(frame(p, 1).mWaitType);
            }
        }
        // every script: a list of three
        CScript* e = script(4);
        c->m_waitType = 5;
        frame(c, 0).mWaitType = 0x99;
        frame(c, 1).mpWaitComponent = component(3);
        frame(c, 1).mWaitType = 0x98;
        d->m_waitComponent = component(3);
        d->m_waitType = 8;
        e->m_waitComponent = component(3);
        e->m_waitType = 9;
        e->m_numReturnAddresses = 1;
        frame(e, 0).mpWaitComponent = component(3);
        frame(e, 0).mWaitType = 0xF3;
        c->mp_next = d;
        d->mp_next = e;
        e->mp_next = nullptr;
        Script::ScriptListHead() = c;
        for (int i : {0, 3}) {
            if (original)
                Original<void(__cdecl*)(Obj::CBaseComponent*)>(0x00472E70)(component(i));
            else
                Script::ComponentDestroyedAllScripts(component(i));
            for (CScript* p : {c, d, e})
                Rec(0xCD0, {p->m_waitType, frame(p, 0).mWaitType, frame(p, 1).mWaitType});
        }
        Script::ScriptListHead() = nullptr;
        // no scripts
        if (original)
            Original<void(__cdecl*)(Obj::CBaseComponent*)>(0x00472E70)(component(0));
        else
            Script::ComponentDestroyedAllScripts(component(0));

        // --- ReceiveEvent: the blocked levels of its type; then the event handler table.
        auto* events = reinterpret_cast<uint32_t*>(arena + 0x1400);
        events[0] = 0xEEEE0001;
        events[4] = 0;
        events[8] = 0xEEEE0002;
        auto event = [&](int i) { return reinterpret_cast<Obj::CEvent*>(events + 4 * i); };
        CScript* f = script(5);
        f->m_numReturnAddresses = 2;
        auto receive = [&](CScript* p, int i) {
            uint32_t result = original
                                  ? Original<uint32_t(__thiscall*)(CScript*, Obj::CEvent*)>(0x00473800)(p, event(i))
                                  : p->ReceiveEvent(event(i));
            Rec(0xEE, {result & 0xFF, p->m_waitType, frame(p, 0).mWaitType, frame(p, 1).mWaitType});
        };
        struct Case {
            uint32_t type, arg, type0, arg0, type1, arg1;
        };
        for (Case k : {Case{3, 0xEEEE0001, 0x13, 0xEEEE0001, 0x23, 0xEEEE0002},
                       Case{3, 0xEEEE0002, 0x3, 0, 0x4, 0xEEEE0001}, Case{4, 0xEEEE0001, 0x3, 0xEEEE0001, 0x3, 0xEEEE0001},
                       Case{3, 0, 0x3, 0xEEEE0002, 0x13, 0xEEEE0001}}) {
            for (int i : {0, 1, 2}) {
                f->m_waitType = k.type;
                f->m_waitArg = k.arg;
                frame(f, 0).mWaitType = k.type0;
                frame(f, 0).mWaitArg = k.arg0;
                frame(f, 1).mWaitType = k.type1;
                frame(f, 1).mWaitArg = k.arg1;
                receive(f, i);
            }
        }
        f->m_eventHandlers = reinterpret_cast<Obj::CEventHandlerTable*>(arena + 0x1500);
        f->m_numReturnAddresses = 0;
        f->m_waitType = 3;
        f->m_waitArg = 0xEEEE0002;
        receive(f, 2);

        // --- UnblockInterruptedScript: from the top, the first interrupt's interrupted level.
        CScript* g = script(6);
        struct Unblock {
            int depth;
            uint8_t flags;
            uint8_t f0, f1, f2;
        };
        for (Unblock k : {Unblock{0, 1, 2, 2, 2}, Unblock{3, 1, 2, 2, 2}, Unblock{3, 0xFE, 0xFD, 2, 0xFF},
                          Unblock{3, 0, 0, 0xFE, 1}, Unblock{3, 0, 2, 0, 0}, Unblock{1, 0, 0xFF, 0, 0},
                          Unblock{1, 1, 0, 0, 0}, Unblock{2, 2, 2, 2, 0}}) {
            g->m_returnAddresses = reinterpret_cast<SReturnAddress*>(arena + 0x2000);
            g->m_numReturnAddresses = k.depth;
            g->m_flags = k.flags;
            for (int i = 0; i < 3; ++i) {
                frame(g, i).mWaitType = 0x5A5A5A50u + i + 1;
                frame(g, i).mFlags = i == 0 ? k.f0 : i == 1 ? k.f1 : k.f2;
            }
            if (original)
                Original<Fn0>(0x00472F40)(g);
            else
                g->UnblockInterruptedScript();
            Rec(0x0B1, {frame(g, 0).mWaitType, frame(g, 1).mWaitType, frame(g, 2).mWaitType});
        }

        // --- CallScript / ReturnFromScript / Interrupt.
        using CallFn = void(__thiscall*)(CScript*, uint32_t, const uint8_t*, Script::CStruct*, Obj::CObject*, bool);
        auto call = [&](CScript* p, uint32_t name, const uint8_t* at, Script::CStruct* params, Obj::CObject* obj,
                        bool interrupted) {
            if (original)
                Original<CallFn>(0x004748E0)(p, name, at, params, obj, interrupted);
            else
                p->CallScript(name, at, params, obj, interrupted);
            Rec(0xCA11, {static_cast<uint32_t>(p->m_numReturnAddresses), Rel(p->m_returnAddresses), Rel(p->m_pc), Rel(p->m_params),
                         p->m_flags, p->m_waitType});
        };
        auto ret = [&](CScript* p, Script::CStruct* results) {
            bool r = original ? Original<bool(__thiscall*)(CScript*, Script::CStruct*)>(0x00473E70)(p, results)
                              : p->ReturnFromScript(results);
            Rec(0x2E7, {r, static_cast<uint32_t>(p->m_numReturnAddresses), Rel(p->m_pc), Rel(p->m_params), p->m_flags, p->m_waitType});
            state(p);
            Rec(0x2E8, {Rel(p->m_waitComponent), Rel(p->m_currentLoop), p->m_onExitScript, p->m_scriptName});
        };
        auto busy = [&](CScript* p, uint32_t seed) {
            // a running level's state: everything distinct
            p->m_pc = code(seed & 7) + 3;
            p->m_params = structure(seed & 7);
            p->m_currentLoop = reinterpret_cast<Script::SLoop*>(arena + 0x1800 + 0x10 * (seed & 7));
            p->m_onExitScript = 0x0E000000u + seed;
            p->m_waitType = (seed % 3 == 0) ? 0 : 0x10 + seed % 12;
            p->m_waitComponent = component(seed & 3);
            p->m_waitArg = 0xA0A00000u + seed;
            p->m_waitCounter = static_cast<int>(seed * 3);
            setTimer(p, 0x40000000u + seed);
            p->m_waitTimerStarted = (seed & 2) != 0; // (not the same bit as the interrupt flag)
            p->m_flags = static_cast<uint8_t>(0xF0 | (seed & 3));
        };
        // not running: SetScript, the interrupt flag
        CScript* h = script(7);
        h->m_flags = 0xF2;
        call(h, 0x11110001, code(1), structure(1), object(1), true);
        h->m_pc = nullptr;
        call(h, 0x11110002, nullptr, nullptr, nullptr, false);
        h->m_flags = 0x03;
        h->m_pc = nullptr;
        call(h, 0x11110003, code(2), nullptr, object(2), false);
        // running: four levels (the third moves the stack to the heap: the frames' other bits from dirty memory)
        CScript* k = script(8);
        k->m_inlineReturnAddresses[0].mWaitType = 0x7770;
        k->m_inlineReturnAddresses[0].mFlags = 0xFC;
        k->m_inlineReturnAddresses[1].mWaitType = 0x8885;
        k->m_inlineReturnAddresses[1].mFlags = 0xA3;
        busy(k, 1);
        k->m_object.mp_object = object(0);
        call(k, 0x22220001, code(3), structure(9), object(1), false);
        busy(k, 2);
        call(k, 0x22220002, code(4), nullptr, nullptr, true);
        busy(k, 3);
        call(k, 0x22220003, code(5), structure(10), object(3), true);
        busy(k, 4);
        k->m_object.mp_object = object(3);
        call(k, 0x22220004, code(6), nullptr, object(3), false);
        busy(k, 6);
        k->m_params = nullptr; // (a level without parameters)
        call(k, 0x22220005, code(7), structure(11), nullptr, false);
        // back down, with and without results
        busy(k, 7);
        ret(k, structure(12));
        ret(k, nullptr);
        k->m_params = nullptr;
        ret(k, structure(13));
        busy(k, 5);
        ret(k, nullptr);
        ret(k, structure(14));
        ret(k, structure(15)); // (the bottom: the script ends, its parameters get the results)
        ret(k, nullptr);
        // a second time up to the heap block (already there: no growth)
        call(k, 0x22220006, code(1), nullptr, nullptr, false);
        call(k, 0x22220007, code(2), nullptr, nullptr, false);
        call(k, 0x22220008, code(3), nullptr, nullptr, false);
        // the inline stack but at depth 2 after a heap block was made elsewhere: not moved again
        CScript* m = script(9);
        busy(m, 8);
        m->m_numReturnAddresses = 1;
        m->m_returnAddresses = reinterpret_cast<SReturnAddress*>(arena + 0x2400);
        call(m, 0x33330001, code(1), nullptr, nullptr, false);
        call(m, 0x33330002, code(1), nullptr, nullptr, false);
        // Interrupt: running and not
        CScript* n = script(10);
        busy(n, 9);
        n->m_object.mp_object = object(2);
        g_csc_update = 5;
        for (int i = 0; i < 2; ++i) {
            using InterruptFn = uint32_t(__thiscall*)(CScript*, uint32_t, Script::CStruct*);
            uint32_t r = original ? Original<InterruptFn>(0x00475750)(n, 0x44440120 + i * 0x10, i ? nullptr : structure(5))
                                  : n->Interrupt(0x44440120 + i * 0x10, i ? nullptr : structure(5));
            Rec(0x1A7, {r, static_cast<uint32_t>(n->m_numReturnAddresses), Rel(n->m_pc), n->m_flags});
            g_csc_update = 1;
            n->m_pc = nullptr;
            n->m_object.mp_object = nullptr;
        }

        // --- event handlers
        CScript* q = script(11);
        auto* arrays = reinterpret_cast<Script::CArray*>(arena + 0x3000);
        auto table = [&]() { return Rel(q->m_eventHandlers); };
        using Fn1 = void(__thiscall*)(CScript*, uint32_t);
        auto group = [&]() {
            if (original)
                Original<Fn1>(0x00472EA0)(q, 0x6A0F);
            else
                q->ClearEventHandlerGroup(0x6A0F);
            if (original)
                Original<Fn1>(0x00473690)(q, 0x7E01);
            else
                q->ClearEventHandler(0x7E01);
            uint32_t printed = original ? Original<uint32_t(__thiscall*)(CScript*)>(0x00472EC0)(q)
                                        : static_cast<uint32_t>(q->PrintEventHandlerTable());
            g_trace.push_back(printed);
        };
        group();
        // made by SetEventHandlers (replace) / SetEventHandler / ResetEventHandlersFromTable
        using SetsFn = void(__thiscall*)(CScript*, Script::CArray*, bool);
        using SetFn = void(__thiscall*)(CScript*, uint32_t, uint32_t, uint32_t, bool, Script::CStruct*);
        using ResetFn = void(__thiscall*)(CScript*, Script::CArray*, uint32_t);
        auto sets = [&](Script::CArray* array, bool replace) {
            if (original)
                Original<SetsFn>(0x004734E0)(q, array, replace);
            else
                q->SetEventHandlers(array, replace);
            g_trace.push_back(table());
        };
        auto set = [&](uint32_t type, uint32_t name, uint32_t grp, bool exception, Script::CStruct* params) {
            if (original)
                Original<SetFn>(0x004735D0)(q, type, name, grp, exception, params);
            else
                q->SetEventHandler(type, name, grp, exception, params);
            g_trace.push_back(table());
        };
        auto reset = [&](Script::CArray* array, uint32_t grp) {
            if (original)
                Original<ResetFn>(0x004736C0)(q, array, grp);
            else
                q->ResetEventHandlersFromTable(array, grp);
            g_trace.push_back(table());
        };
        sets(&arrays[0], true);
        sets(&arrays[1], false);
        group();
        q->m_eventHandlers = nullptr;
        set(0x7E02, 0x5C02, 0x6A02, true, structure(3));
        set(0x7E03, 0x5C03, 0x6A03, false, nullptr);
        q->m_eventHandlers = nullptr;
        g_csc_allocFail = true;
        set(0x7E04, 0x5C04, 0x6A04, true, nullptr); // (no table: the original calls through null)
        g_csc_allocFail = false;
        // the tables: handler structures {flag, type, script, [params]}
        auto* comps = reinterpret_cast<Script::CComponent*>(arena + 0x3400);
        auto* structs = reinterpret_cast<uint32_t*>(arena + 0x3300);
        auto handler = [&](int i, uint32_t flag, uint32_t type, uint32_t name, Script::CStruct* params) {
            Script::CComponent* c4 = &comps[i * 4];
            c4[0].checksum = flag;
            c4[0].next = &c4[1];
            c4[1].checksum = type;
            c4[1].next = &c4[2];
            c4[2].checksum = name;
            c4[2].next = params ? &c4[3] : nullptr;
            c4[3].structure = params;
            c4[3].next = nullptr;
            structs[i * 4] = reinterpret_cast<uint32_t>(c4);
            return reinterpret_cast<uint32_t>(&structs[i * 4]);
        };
        auto* elements = reinterpret_cast<uint32_t*>(arena + 0x3200);
        elements[0] = handler(0, 0x80367192, 0x7E10, 0x5C10, structure(4));
        elements[1] = handler(1, 0x12345678, 0x7E11, 0x5C11, nullptr);
        elements[2] = handler(2, 0x80367193, 0x7E12, 0x5C12, structure(6));
        arrays[2].m_size = 3;
        arrays[2].m_elements = elements;
        arrays[3].m_size = 1;
        arrays[3].m_single = handler(3, 0x80367192, 0x7E13, 0x5C13, nullptr);
        arrays[4].m_size = 0;
        q->m_eventHandlers = nullptr;
        reset(&arrays[4], 0x6A20); // nothing, and no table made
        reset(&arrays[2], 0x6A21);
        reset(&arrays[3], 0x6A22);
        reset(&arrays[4], 0x6A23);
        group();
        q->m_eventHandlers = nullptr;
        sets(&arrays[2], false);
        g_csc_allocFail = true;
        q->m_eventHandlers = nullptr;
        reset(&arrays[4], 0x6A24);

        // The whole arena.
        std::vector<uint32_t> result = g_trace;
        std::vector<uint32_t> image(kArenaSize / 4);
        std::memcpy(image.data(), arena, kArenaSize);
        for (uint32_t& word : image) {
            word = ArenaWordOrHigh(g_csc_arenas, kArenaSize, word);
        }
        result.insert(result.end(), image.begin(), image.end());
        g_trace.clear();
        return result;
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    Cleanup();
    Compare(expected, actual,
            "CScript waits (counter, one per frame, timer, real timer, components, blocked), IsBlocked / StopWaiting / "
            "Block, ComponentDestroyed (+ every script, ECX kept), ReceiveEvent, UnblockInterruptedScript, "
            "CallScript (start, push, stack growth), ReturnFromScript, Interrupt, the event handler methods");
}

} // namespace

SELFTEST(Early, TestCScriptWait, Cleanup);
