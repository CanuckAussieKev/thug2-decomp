// Self-tests for the script interpreter (src/game/script/cscript_exec.cpp): CScript::Update and the statements it
// runs, original against ours, on real token streams run by stand-in scripts (made and set up with the game's own
// Construct / SetScript) over a stand-in memory manager, symbol table and script cache.
//
// The original run uses the game's interpreter throughout; in our run the game's entry points of the 13 rewritten
// functions jump to ours (as when hooked), so the nested Updates the game's Interrupt makes are ours too. Group C's
// functions (UpdateWait, CallScript, ReturnFromScript, Interrupt) and group A's are the game's in both runs. The
// script cache's GetScript / ReleaseScript, FindObject, the GameNet check, Dbg::Printf and the checksum names are
// recording stand-ins; C functions and the objects' member functions are the test's own (recording).

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <new>
#include <utility>
#include <vector>

#include "game/core/alloc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/net/gamenet_manager.h"
#include "game/script/cache.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/eval.h"
#include "game/script/names.h"
#include "game/script/params.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using Script::CComponent;
using Script::CMemberFunctionEntry;
using Script::CScript;
using Script::CStruct;
using Script::CSymbolTableEntry;

constexpr uint32_t kArenaSize = 0x40000;
ArenaPair<kArenaSize> g_csb_storage; // (aligned to 64K: see ArenaWordOrHigh)
uint8_t (*const g_csb_arenas)[kArenaSize] = g_csb_storage.Get();

// --- token streams ----------------------------------------------------------------------------------------------

struct Qb {
    std::vector<uint8_t> b;
    Qb& u8(uint32_t v)
    {
        b.push_back(static_cast<uint8_t>(v));
        return *this;
    }
    Qb& u16(uint32_t v) { return u8(v).u8(v >> 8); }
    Qb& u32(uint32_t v) { return u8(v).u8(v >> 8).u8(v >> 16).u8(v >> 24); }
    Qb& eol() { return u8(0x01); }
    Qb& line(uint32_t n) { return u8(0x02).u32(n); }
    Qb& name(uint32_t c) { return u8(0x16).u32(c); }
    Qb& integer(int v) { return u8(0x17).u32(static_cast<uint32_t>(v)); }
    Qb& assign(uint32_t c) { return name(c).u8(0x07); }
    Qb& param(uint32_t c) { return u8(0x2D).u8(0x16).u32(c); }
    Qb& all() { return u8(0x2C); }
    Qb& pointer(uint8_t token, const void* p) { return u8(token).u32(reinterpret_cast<uint32_t>(p)); }
    // A 2-byte jump (if 47, else 48, 49): the position of its offset, for Land.
    size_t Jump(uint8_t token)
    {
        u8(token);
        size_t at = b.size();
        u16(0);
        return at;
    }
    // The jump whose offset is at `at` goes here (offset = target - the offset's position).
    Qb& Land(size_t at)
    {
        uint32_t offset = static_cast<uint32_t>(b.size() - at);
        b[at] = static_cast<uint8_t>(offset);
        b[at + 1] = static_cast<uint8_t>(offset >> 8);
        return *this;
    }
    // A random block (as the parser test's): each branch then a 2E jump past the rest.
    Qb& Random(uint8_t token, const std::vector<std::pair<int, Qb>>& branches)
    {
        uint32_t n = static_cast<uint32_t>(branches.size());
        u8(token).u32(n);
        for (auto& branch : branches)
            u16(static_cast<uint32_t>(branch.first));
        size_t offsets = b.size();
        for (uint32_t i = 0; i < n; ++i)
            u32(0);
        std::vector<size_t> jumps;
        for (uint32_t i = 0; i < n; ++i) {
            size_t at = offsets + 4 * i;
            uint32_t offset = static_cast<uint32_t>(b.size() - at - 4);
            std::memcpy(&b[at], &offset, 4);
            b.insert(b.end(), branches[i].second.b.begin(), branches[i].second.b.end());
            jumps.push_back(b.size());
            u8(0x2E).u32(0);
        }
        for (size_t at : jumps) {
            uint32_t offset = static_cast<uint32_t>(b.size() - at - 5);
            std::memcpy(&b[at + 1], &offset, 4);
        }
        return *this;
    }
};

// Names (checksums) used by the scripts.
constexpr uint32_t CF_REC = 0xC0000F01, CF_WAIT = 0xC0000F02, CF_BLOCK = 0xC0000F03, CF_STOP = 0xC0000F04,
                   CF_ONEXIT = 0xC0000F05, CF_INTERRUPT = 0xC0000F06,
                   CF_END = 0xC0000F07;
constexpr uint32_t MF_ENTRY = 0xC0000E01, MF_NOENTRY = 0xC0000E02, MF_COMP = 0xC0000E03, MF_NAME = 0xC0000E04;
constexpr uint32_t SUB1 = 0x5C0000A1, SUB_WAIT = 0x5C0000A2, SUB_LOOP = 0x5C0000A3, SUB_EXIT = 0x5C0000A4,
                   INT1 = 0x5C0000A5, INT_WAIT = 0x5C0000A6, ONEXIT1 = 0x5C0000A7, ONEXIT_WAIT = 0x5C0000A8,
                   ALIAS_SUB1 = 0x5C0000A9, INT_END = 0x5C0000AA, UNKNOWN = 0x5C0000AF;
constexpr uint32_t OBJ_B = 0x0B000B0B, OBJ_MISSING = 0x0B00DEAD;
constexpr uint32_t P_RET = 0x0000E7E7, P_TAG = 0x0000E001, P_X = 0x0000E002, P_Y = 0x0000E003, P_Z = 0x0000E004,
                   P_PN = 0x0000E005, P_DEF = 0x0000E006, P_R = 0x0000E007, P_W = 0x0000E008;
constexpr uint32_t kComponentType = 0x00C0C0C0;
constexpr uint32_t kQuietScript = 0xFB11E6CD;

// --- stand-in objects, member functions, C functions -------------------------------------------------------------

std::vector<uint8_t> g_code; // every script's tokens
uint32_t PcOffset(const uint8_t* pc)
{
    if (!pc)
        return 0;
    return static_cast<uint32_t>(pc - g_code.data()) + 1;
}

void Describe(const CStruct* s)
{
    if (!s) {
        Record({0x5D00});
        return;
    }
    Record({0x5D01});
    for (const CComponent* c = s->m_head; c; c = c->next)
        Record({c->typeAndFlag, c->name, c->raw});
    Record({0x5D02});
}

uint32_t RetParam(CStruct* params, uint32_t fallback)
{
    int value = 0;
    return params && params->GetInteger(P_RET, &value, 0) ? static_cast<uint32_t>(value) : fallback;
}

struct FakeComponent {
    void** vtable;
    uint32_t type;
    uint32_t unknown8[2];
    FakeComponent* next; // +0x10
};
struct FakeObject {
    void** vtable;
    void* smartPointers;
    uint8_t unknown8[0x100 - 8];
    FakeComponent* components; // +0x100
};
static_assert(offsetof(FakeObject, components) == 0x100, "the component list at +0x100");

// (thiscall through fastcall: ECX the object, EDX unused, the stack arguments popped by the callee)
uint32_t __fastcall ObjectEntry(FakeObject* self, void*, CMemberFunctionEntry* entry, CStruct* params, CScript* script)
{
    Record({0x0B02, reinterpret_cast<uint32_t>(self), reinterpret_cast<uint32_t>(entry), reinterpret_cast<uint32_t>(script)});
    Describe(params);
    return 0xABCD0000u | (RetParam(params, 1) & 0xFF); // only AL counts
}
uint32_t __fastcall ObjectName(FakeObject* self, void*, uint32_t name, CStruct* params, CScript* script)
{
    Record({0x0B03, reinterpret_cast<uint32_t>(self), name, reinterpret_cast<uint32_t>(script)});
    Describe(params);
    return 0x12340000u | (RetParam(params, 1) & 0xFF);
}
uint32_t __fastcall ComponentEntry(FakeComponent* self, void*, CMemberFunctionEntry* entry, CStruct* params,
                                   CScript* script)
{
    Record({0x0C0B, reinterpret_cast<uint32_t>(self), reinterpret_cast<uint32_t>(entry), reinterpret_cast<uint32_t>(script)});
    Describe(params);
    return RetParam(params, 0x100); // the whole of EAX counts
}
void* g_objectVtable[8] = {nullptr, nullptr, reinterpret_cast<void*>(&ObjectEntry), reinterpret_cast<void*>(&ObjectName)};
void* g_componentVtable[12] = {};

FakeObject g_objA, g_objB, g_objZ;
FakeComponent g_compA, g_compB, g_compOther;
CMemberFunctionEntry g_entryObj = {0x11, 0x57525413}, g_entryObj2 = {0x22, 0x0ADA4E74}, g_entryComp = {0x33, kComponentType};

void ResetObjects()
{
    g_componentVtable[11] = reinterpret_cast<void*>(&ComponentEntry);
    for (FakeObject* o : {&g_objA, &g_objB, &g_objZ}) {
        std::memset(o, 0, sizeof(*o));
        o->vtable = g_objectVtable;
    }
    for (FakeComponent* c : {&g_compA, &g_compB, &g_compOther}) {
        std::memset(c, 0, sizeof(*c));
        c->vtable = g_componentVtable;
        c->type = kComponentType;
    }
    g_compOther.type = 0x0DD0DD0D;
    g_objA.components = &g_compOther; // (searched past)
    g_compOther.next = &g_compA;
    g_objB.components = &g_compB;
}

bool __cdecl CfRecord(CStruct* params, CScript* script)
{
    Record({0xCF01, reinterpret_cast<uint32_t>(script), script->m_scriptName, PcOffset(script->m_pc),
            reinterpret_cast<uint32_t>(script->m_object.Get()), script->m_flags});
    Describe(params);
    return (RetParam(params, 1) & 0xFF) != 0;
}
bool __cdecl CfWait(CStruct* params, CScript* script)
{
    int frames = 0;
    params->GetInteger(0u, &frames, 0);
    GameFunction<void(__thiscall*)(CScript*, int)>(0x00472BD0)(script, frames); // Wait
    return true;
}
bool __cdecl CfBlock(CStruct*, CScript* script)
{
    GameFunction<void(__thiscall*)(CScript*, uint32_t)>(0x00472FD0)(script, 0); // Block
    return true;
}
bool __cdecl CfStop(CStruct* params, CScript* script)
{
    bool result = (RetParam(params, 1) & 0xFF) != 0; // (ClearScript frees the parameters)
    GameFunction<void(__thiscall*)(CScript*)>(0x00473A80)(script); // ClearScript
    return result;
}
bool __cdecl CfEnd(CStruct*, CScript* script)
{
    script->m_pc = nullptr; // (ends the script without ClearScript: its flags are kept)
    return true;
}
bool __cdecl CfOnExit(CStruct* params, CScript* script)
{
    uint32_t name = 0;
    params->GetChecksum(0u, &name, 0);
    script->m_onExitScript = name;
    return true;
}
bool __cdecl CfInterrupt(CStruct* params, CScript* script)
{
    uint32_t name = 0;
    params->GetChecksum(0u, &name, 0);
    int result = GameFunction<int(__thiscall*)(CScript*, uint32_t, CStruct*)>(0x00475750)(script, name, nullptr);
    Record({0x1A7, static_cast<uint32_t>(result), PcOffset(script->m_pc)});
    return true;
}

// --- the scripts --------------------------------------------------------------------------------------------------

struct MainScript {
    uint32_t name;
    FakeObject* object;
    Qb code;
    size_t at = 0;
};
std::map<uint32_t, size_t> g_scriptAt; // callable scripts' code (offsets into g_code)
std::vector<MainScript> g_mains;

Qb Rec(uint32_t tag)
{
    Qb q;
    q.name(CF_REC).assign(P_TAG).integer(static_cast<int>(tag)).eol();
    return q;
}
Qb& Append(Qb& q, const Qb& more)
{
    q.b.insert(q.b.end(), more.b.begin(), more.b.end());
    return q;
}

void BuildScripts()
{
    g_code.clear();
    g_scriptAt.clear();
    g_mains.clear();
    std::vector<std::pair<uint32_t, Qb>> callable;
    {
        Qb q; // defaults, a call with all its parameters, return with arguments
        q.assign(P_DEF).integer(7).eol().name(CF_REC).all().eol().u8(0x29).assign(P_R).integer(5).eol().u8(0x24);
        callable.push_back({SUB1, q});
    }
    {
        Qb q;
        q.eol().name(CF_WAIT).integer(2).eol();
        Append(q, Rec(0x51)).u8(0x24);
        callable.push_back({SUB_WAIT, q});
    }
    {
        Qb q; // a loop in a called script (the caller is in its inline loop)
        q.eol().u8(0x20);
        Append(q, Rec(0x52)).u8(0x21).integer(2).eol().u8(0x24);
        callable.push_back({SUB_LOOP, q});
    }
    {
        Qb q; // an OnExitRun script in a called script, then return
        q.eol().name(CF_ONEXIT).name(ONEXIT1).eol().u8(0x29).assign(P_W).integer(9).eol().u8(0x24);
        callable.push_back({SUB_EXIT, q});
    }
    {
        Qb q;
        q.eol();
        Append(q, Rec(0x53)).u8(0x24);
        callable.push_back({INT1, q});
    }
    {
        Qb q;
        q.eol().name(CF_WAIT).integer(1).eol();
        Append(q, Rec(0x54)).u8(0x24);
        callable.push_back({INT_WAIT, q});
    }
    {
        Qb q;
        q.eol();
        Append(q, Rec(0x55)).u8(0x24);
        callable.push_back({ONEXIT1, q});
    }
    {
        Qb q;
        q.eol().name(CF_WAIT).integer(1).eol();
        Append(q, Rec(0x56)).u8(0x24);
        callable.push_back({ONEXIT_WAIT, q});
    }
    {
        Qb q;
        q.eol().name(CF_END).eol().u8(0x24);
        callable.push_back({INT_END, q});
    }

    auto main = [&](uint32_t name, FakeObject* object) -> Qb& {
        g_mains.push_back({name, object, {}});
        return g_mains.back().code;
    };
    {
        // M1: the tokens Update skips, C functions by name and pointer, assignments, expressions, <name>, <...>,
        // unknown names
        Qb& q = main(0x3A000001, &g_objA);
        q.name(CF_REC).eol(); // the defaults: an unnamed name (for <...>)
        q.line(10).u8(0x28).u8(0x3D).eol();
        q.name(CF_REC).assign(P_TAG).integer(1).eol();
        q.pointer(0x43, reinterpret_cast<const void*>(&CfRecord)).assign(P_TAG).integer(2).eol();
        q.assign(P_X).integer(5).eol();
        q.assign(P_X).u8(0x0E).integer(1).u8(0x0B).integer(2).u8(0x0F).eol(); // replaces x
        q.assign(P_Y).param(0x0000BAD0).eol();                                 // nothing: not added
        q.assign(P_PN).name(CF_REC).eol();
        q.assign(P_Z).all().eol();
        q.param(P_PN).assign(P_TAG).integer(3).eol();        // <pn> names CF_REC
        q.all().assign(P_TAG).integer(4).eol();              // <...>: the unnamed checksum (CF_REC)
        q.param(P_X).u8(0x07).integer(6).eol();              // <x> = 6: the 4 bytes before '=' (x)
        q.u8(0x0E).integer(1).u8(0x0F).eol();                // (1): true, nothing run
        q.u8(0x0E).integer(0).u8(0x0F).eol();
        q.u8(0x0E).name(CF_REC).u8(0x0F).assign(P_TAG).integer(5).eol(); // (name): runs it
        q.integer(5).eol();                                  // name 0: unknown
        q.name(UNKNOWN).integer(1).eol();
        q.name(CF_REC).all().eol();
        q.u8(0x24);
    }
    {
        // M2: script calls (an alias names SUB1), a called script that waits
        Qb& q = main(0x3A000002, &g_objA);
        q.assign(P_X).integer(1).eol();
        q.name(SUB1).assign(P_TAG).integer(1).eol();
        q.name(ALIAS_SUB1).assign(P_TAG).integer(2).eol();
        q.name(SUB_WAIT).eol();
        q.name(CF_REC).all().eol();
        q.u8(0x24);
    }
    {
        // M3: if / else / if not; conditions: C functions, an expression, a script call, a member function,
        // one that stops the script
        Qb& q = main(0x3A000003, &g_objA);
        q.eol();
        auto ifElse = [&](bool negate, const Qb& condition, uint32_t tag) {
            size_t j = q.Jump(0x47);
            if (negate)
                q.u8(0x39);
            Append(q, condition);
            Append(q, Rec(tag));
            size_t e = q.Jump(0x48);
            q.Land(j).eol();
            Append(q, Rec(tag + 1));
            q.Land(e).u8(0x28).eol();
        };
        Qb t, f, zero, sub, member, memberFalse;
        t.name(CF_REC).assign(P_RET).integer(1).eol();
        f.name(CF_REC).assign(P_RET).integer(0).eol();
        zero.u8(0x0E).integer(0).u8(0x0F).eol();
        sub.name(SUB1).eol();
        member.u8(0x44).u32(MF_NAME).eol();
        memberFalse.u8(0x44).u32(MF_NAME).assign(P_RET).integer(0).eol();
        ifElse(false, t, 0x10);
        ifElse(false, f, 0x20);
        ifElse(true, f, 0x30);
        ifElse(true, t, 0x40);
        ifElse(false, zero, 0x50);
        ifElse(false, sub, 0x60);
        ifElse(false, member, 0x70);
        ifElse(false, memberFalse, 0x80);
        Qb five, nameZero, assignment;
        five.u8(0x0E).integer(5).u8(0x0F).eol();
        nameZero.u8(0x0E).name(0).u8(0x0F).name(CF_REC).assign(P_RET).integer(1).eol(); // (0) CF_REC: runs CF_REC
        assignment.assign(P_W).integer(1).eol();
        ifElse(false, five, 0xB0);
        ifElse(false, nameZero, 0xC0);
        ifElse(true, assignment, 0xD0);
        {
            size_t j = q.Jump(0x47); // an if without else
            Append(q, f);
            Append(q, Rec(0x90));
            q.Land(j).u8(0x28).eol();
        }
        Qb stop;
        stop.name(CF_STOP).assign(P_RET).integer(0).eol();
        ifElse(false, stop, 0xA0); // the script ends in the condition: no jump
        q.u8(0x24);
    }
    {
        // M4: loops: counted, nested (the loop block moves to the heap), break (over a nested loop; the
        // repeat's arguments parsed without the script's parameters), break out of an inner loop
        Qb& q = main(0x3A000004, &g_objA);
        q.assign(P_X).integer(3).eol();
        q.u8(0x20);
        Append(q, Rec(0x10)).u8(0x21).integer(3).eol();
        q.u8(0x20).u8(0x20);
        Append(q, Rec(0x20)).u8(0x21).integer(2).eol().u8(0x21).integer(2).eol();
        q.u8(0x20);
        Append(q, Rec(0x30)).u8(0x22).eol().u8(0x20);
        Append(q, Rec(0x31)).u8(0x21).integer(2).eol().u8(0x21).all().eol();
        q.u8(0x20).u8(0x20);
        Append(q, Rec(0x40)).u8(0x22).eol().u8(0x21).integer(5).eol();
        Append(q, Rec(0x41)).u8(0x21).param(P_X).eol();
        q.name(CF_REC).all().eol();
        q.u8(0x24);
    }
    {
        // M4b: ends right after a break (its repeat's arguments, parsed without the parameters, stay)
        Qb& q = main(0x3A000011, &g_objA);
        q.assign(P_X).integer(3).eol().u8(0x20).u8(0x22).eol().u8(0x21).all().eol().u8(0x24);
    }
    {
        // M5: a loop in the inline loop calls a script with a loop (the caller's saved loop is moved)
        Qb& q = main(0x3A000005, &g_objA);
        q.eol().u8(0x20).name(SUB_LOOP).eol();
        Append(q, Rec(0x10)).u8(0x21).integer(2).eol();
        q.u8(0x24);
    }
    {
        // M6: switch: a match through fall-through labels, a scan through a case's statements (nested
        // switches, tokens skipped), default, no match, an empty case line
        Qb& q = main(0x3A000006, &g_objA);
        q.eol().assign(P_X).integer(2).eol();
        q.u8(0x3C).param(P_X).eol();
        std::vector<size_t> ends;
        q.u8(0x3E);
        size_t j1 = q.Jump(0x49);
        q.integer(1).eol();
        Append(q, Rec(0x11));
        ends.push_back(q.Jump(0x49));
        q.Land(j1).u8(0x3E);
        size_t j2 = q.Jump(0x49);
        q.integer(2).eol();
        q.Land(j2); // (case 2's next: its own end jump - skipped as a token)
        ends.push_back(q.Jump(0x49));
        q.u8(0x3E);
        size_t j3 = q.Jump(0x49);
        q.integer(3).eol();
        Append(q, Rec(0x12));
        ends.push_back(q.Jump(0x49));
        q.Land(j3).u8(0x3F);
        size_t jd = q.Jump(0x49);
        Append(q, Rec(0x13));
        q.Land(jd);
        for (size_t at : ends)
            q.Land(at);
        q.u8(0x3D).eol();
        auto simpleSwitch = [&](int value, bool withDefault, bool scanBody, uint32_t tag, bool stray = false) {
            q.u8(0x3C).integer(value).eol();
            std::vector<size_t> e;
            q.u8(0x3E);
            size_t j = q.Jump(0x49);
            q.integer(1).eol();
            if (scanBody) {
                q.Land(j); // the scan goes through this case's statements
                Append(q, Rec(tag));
                q.u8(0x3C).integer(2).eol().u8(0x3E);
                size_t k = q.Jump(0x49);
                q.integer(2).eol().u8(0x3C).integer(1).eol().u8(0x3D).eol();
                q.Land(k).u8(0x3D).eol();
            } else {
                Append(q, Rec(tag));
                if (stray)
                    q.u8(0x3D); // (an endswitch token in a case jumped over)
            }
            e.push_back(q.Jump(0x49));
            if (!scanBody)
                q.Land(j);
            q.eol().u8(0x3E); // (an end of line before the next case: skipped by the scan)
            size_t j2b = q.Jump(0x49);
            q.eol(); // an empty case line: no match
            Append(q, Rec(tag + 1));
            e.push_back(q.Jump(0x49));
            q.Land(j2b);
            if (withDefault) {
                q.u8(0x3F);
                size_t d = q.Jump(0x49);
                Append(q, Rec(tag + 2));
                q.Land(d);
            }
            for (size_t at : e)
                q.Land(at);
            q.u8(0x3D).eol();
        };
        simpleSwitch(5, true, true, 0x20);
        simpleSwitch(9, false, false, 0x30);
        simpleSwitch(1, true, false, 0x40);
        simpleSwitch(7, true, false, 0x60, true);
        q.assign(P_Z).u8(0x05).integer(1).integer(2).u8(0x06).eol(); // a switch on an array
        q.u8(0x3C).param(P_Z).eol().u8(0x3E);
        size_t js = q.Jump(0x49);
        q.integer(1).eol();
        Append(q, Rec(0x70));
        q.Land(js).u8(0x3D).eol();
        q.u8(0x3C).eol().u8(0x3E); // an empty switch line
        size_t je = q.Jump(0x49);
        q.integer(0).eol();
        Append(q, Rec(0x50));
        q.Land(je).u8(0x3D).eol();
        q.u8(0x24);
    }
    {
        // M7: member functions on the script's object and on obj: objects; obj: with a missing object
        Qb& q = main(0x3A000007, &g_objA);
        q.eol();
        q.u8(0x44).u32(MF_NAME).assign(P_TAG).integer(1).eol();
        q.pointer(0x46, &g_entryObj).assign(P_TAG).integer(2).eol();
        q.pointer(0x46, &g_entryObj2).eol();
        q.pointer(0x46, &g_entryComp).eol();
        q.pointer(0x46, &g_entryComp).assign(P_RET).integer(0).eol();
        q.name(MF_ENTRY).assign(P_TAG).integer(3).eol();
        q.name(MF_NOENTRY).assign(P_TAG).integer(4).eol();
        q.name(MF_COMP).eol();
        q.name(OBJ_B).u8(0x42).u8(0x44).u32(MF_NAME).assign(P_TAG).integer(5).eol();
        q.name(OBJ_B).u8(0x42).name(MF_ENTRY).eol();
        q.name(OBJ_B).u8(0x42).name(MF_NOENTRY).eol();
        q.name(OBJ_B).u8(0x42).name(MF_COMP).eol();
        q.name(OBJ_B).u8(0x42).name(SUB1).assign(P_TAG).integer(6).eol();
        q.name(OBJ_B).u8(0x42).name(ALIAS_SUB1).eol();
        q.name(OBJ_B).u8(0x42).name(CF_REC).eol();                  // a C function: not run, false
        {
            size_t j = q.Jump(0x47);
            q.name(OBJ_B).u8(0x42).name(CF_REC).eol();
            Append(q, Rec(0x10));
            q.Land(j).u8(0x28).eol();
        }
        q.name(OBJ_B).u8(0x42).u8(0x2E).u32(5).integer(9).name(MF_NOENTRY).eol(); // a jump after the ':'
        q.name(OBJ_MISSING).u8(0x42).name(MF_NOENTRY).eol();        // the ':' then looks for object 0
        q.u8(0x0E).name(OBJ_B).u8(0x0F).u8(0x42).name(MF_NOENTRY).eol(); // (obj): the expression's name
        q.u8(0x24);
    }
    {
        // M8: member functions without an object
        Qb& q = main(0x3A000008, nullptr);
        q.eol();
        q.u8(0x44).u32(MF_NAME).eol();
        q.pointer(0x46, &g_entryObj).eol();
        q.name(MF_ENTRY).eol();
        q.name(MF_NOENTRY).eol();
        size_t j = q.Jump(0x47);
        q.u8(0x44).u32(MF_NAME).eol();
        Append(q, Rec(0x10));
        size_t e = q.Jump(0x48);
        q.Land(j);
        Append(q, Rec(0x11));
        q.Land(e).u8(0x28).eol();
        q.u8(0x24);
    }
    {
        // M9a: OnExitRun at '$'
        Qb& q = main(0x3A000009, &g_objA);
        q.eol();
        Append(q, Rec(0x10));
        q.name(CF_ONEXIT).name(ONEXIT1).eol().u8(0x24);
    }
    {
        // M9b: an OnExitRun script that waits, at return (the quirk: return's arguments read from it)
        Qb& q = main(0x3A00000A, &g_objA);
        q.eol().name(CF_ONEXIT).name(ONEXIT_WAIT).eol().u8(0x29).assign(P_R).integer(1).eol().u8(0x24);
    }
    {
        // M9c: an interrupt that ends at once; M9d: one that waits
        Qb& q = main(0x3A00000B, &g_objA);
        q.eol().name(CF_INTERRUPT).name(INT1).eol();
        Append(q, Rec(0x10)).u8(0x24);
        Qb& r = main(0x3A00000C, &g_objA);
        r.eol().name(CF_INTERRUPT).name(INT_WAIT).eol();
        Append(r, Rec(0x10)).u8(0x24);
    }
    {
        // M9e: OnExitRun in a called script, at its return
        Qb& q = main(0x3A00000D, &g_objA);
        q.eol().name(SUB_EXIT).eol();
        Append(q, Rec(0x10)).name(CF_REC).all().eol().u8(0x24);
    }
    {
        // M10: waits (0 frames, 2 frames), blocked (unblocked by the test), the script stopped
        Qb& q = main(0x3A00000E, &g_objA);
        q.eol().name(CF_WAIT).integer(0).eol().name(CF_WAIT).integer(2).eol();
        Append(q, Rec(0x10)).name(CF_BLOCK).eol();
        Append(q, Rec(0x11)).name(CF_STOP).eol();
        Append(q, Rec(0x12)).u8(0x24);
    }
    {
        // M11: the script whose missing names aren't reported
        Qb& q = main(kQuietScript, &g_objA);
        q.eol().name(UNKNOWN).integer(1).eol();
        q.name(OBJ_B).u8(0x42).name(UNKNOWN).eol();
        {
            size_t k = q.Jump(0x47);
            q.name(OBJ_B).u8(0x42).name(UNKNOWN).eol();
            Append(q, Rec(0x20));
            q.Land(k).u8(0x28).eol();
        }
        size_t j = q.Jump(0x47);
        q.u8(0x39).name(UNKNOWN).eol();
        Append(q, Rec(0x10));
        q.Land(j).u8(0x28).eol();
        q.u8(0x24);
    }
    {
        // M12: jumps and random blocks as statements
        Qb& q = main(0x3A000010, &g_objA);
        q.eol().u8(0x2E);
        size_t at = q.b.size();
        q.u32(0);
        Append(q, Rec(0x10));
        uint32_t skip = static_cast<uint32_t>(q.b.size() - at - 4);
        std::memcpy(&q.b[at], &skip, 4);
        size_t j = q.Jump(0x49);
        Append(q, Rec(0x11));
        q.Land(j);
        q.u8(0x20);
        q.Random(0x2F, {{1, Rec(0x20)}, {2, Rec(0x21)}});
        q.Random(0x37, {{2, Rec(0x22)}, {1, Rec(0x23)}});
        q.Random(0x40, {{1, Rec(0x24)}, {1, Rec(0x25)}, {1, Rec(0x26)}});
        q.Random(0x41, {{1, Rec(0x27)}, {1, Rec(0x28)}, {1, Rec(0x29)}});
        q.u8(0x21).integer(4).eol();
        q.u8(0x24);
    }

    for (auto& c : callable) {
        g_scriptAt[c.first] = g_code.size();
        g_code.insert(g_code.end(), c.second.b.begin(), c.second.b.end());
    }
    for (MainScript& m : g_mains) {
        m.at = g_code.size();
        g_code.insert(g_code.end(), m.code.b.begin(), m.code.b.end());
    }
    g_code.push_back(0x24);
}

// --- the test ------------------------------------------------------------------------------------------------------

std::vector<Saved> g_csb_saved;
CSymbolTableEntry** g_csb_savedTable = nullptr;
bool g_csb_swapped = false;

void RestoreCsB()
{
    RemoveStandIns();
    if (!g_csb_swapped)
        return;
    RestoreAll(g_csb_saved);
    GameVariable<CSymbolTableEntry**>(0x006F3624) = g_csb_savedTable;
    g_csb_swapped = false;
}

// The rewritten functions: game address, ours.
std::vector<std::pair<uint32_t, uint32_t>> OurFunctions()
{
    using namespace Script;
    return {
        {0x004750B0, AddressOf(&CScript::Update)},
        {0x00474B40, AddressOf(&CScript::ExecuteCommand)},
        {0x00475050, AddressOf(&CScript::ExecuteIf)},
        {0x00473C60, AddressOf(&CScript::ExecuteSwitch)},
        {0x00472A70, AddressOf(&CScript::SkipToEndOfSwitch)},
        {0x00473220, AddressOf(&CScript::ExecuteBegin)},
        {0x00472AB0, AddressOf(&CScript::ExecuteRepeat)},
        {0x00472B50, AddressOf(&CScript::ExecuteBreak)},
        {0x00472950, AddressOf(&CScript::ParseFunctionParams)},
        {0x004729E0, AddressOf(&CScript::ReadName)},
        {0x00472980, AddressOf(static_cast<bool (*)(CMemberFunctionEntry*, CStruct*, Obj::CObject*, CScript*)>(
                         &Script::CallMemberFunction))},
        {0x004731A0, AddressOf(static_cast<bool (CScript::*)(CMemberFunctionEntry*, Obj::CObject*)>(
                         &CScript::CallMemberFunction))},
        {0x004731E0,
         AddressOf(static_cast<bool (CScript::*)(uint32_t, Obj::CObject*)>(&CScript::CallMemberFunction))},
    };
}

void TestScriptInterpreter()
{
    using namespace Script;
    using Mem::Heap;
    using Mem::Manager;
    using Mem::Region;
    if (GameVariable<void*>(0x007D1800)) {
        Log("self-test: TestScriptInterpreter skipped (the memory manager is already set up)");
        return;
    }
    g_csb_saved = {{0x006F2370, 0x200, {}}, {0x006F2568, 16, {}}, {0x006F2348, 16, {}}, {0x006F2358, 12, {}},
                   {0x006F2578, 16, {}},    {0x006F3530, 4, {}},  {0x006F3538, 4, {}},  {0x006F353C, 4, {}},
                   {0x006F3540, 4, {}},     {0x006F352C, 1, {}},  {0x006F3544, 2, {}},  {0x00690FC8, 24, {}},
                   {0x007CC979, 1, {}},     {0x006F3570, 24, {}}, {0x006F2338, 16, {}}, {0x006F3614, 16, {}},
                   {0x006F22B4, 16, {}},    {0x006F3644, 16, {}}, {0x006F3654, 16, {}}, {0x006F362C, 16, {}},
                   {0x006F3558, 4, {}},     {0x007CCE14, 4, {}},  {0x007D1800, 4, {}}};
    SaveAll(g_csb_saved);
    g_csb_savedTable = GameVariable<CSymbolTableEntry**>(0x006F3624);
    g_csb_swapped = true;
    static CSymbolTableEntry* table[0x1000];
    static CSymbolTableEntry symbols[32];
    GameVariable<CSymbolTableEntry**>(0x006F3624) = table;
    BuildScripts();

    static uint32_t s_cache[9];
    static const char s_nameText[] = "a name";
    StandIns& s = *new StandIns;
    // The script cache (0x006F3558 points at a stand-in): GetScript / ReleaseScript.
    s.At({0x00475E80, AddressOf(&CScriptCache::GetScript)}, 4, [](Regs& r) -> uint32_t {
        Record({0x6E70, r.args[0]});
        auto it = g_scriptAt.find(r.args[0]);
        return it == g_scriptAt.end() ? 0 : reinterpret_cast<uint32_t>(g_code.data() + it->second);
    });
    s.At({0x00475D60, AddressOf(&CScriptCache::ReleaseScript)}, 4, [](Regs& r) -> uint32_t {
        Record({0x6E71, r.args[0]});
        return 0;
    });
    s.At({0x00460AD0, AddressOf(&Obj::FindObject)}, 0, [](Regs& r) -> uint32_t {
        Record({0xF0B1, r.args[0]});
        if (r.args[0] == OBJ_B)
            return reinterpret_cast<uint32_t>(&g_objB);
        if (r.args[0] == 0)
            return reinterpret_cast<uint32_t>(&g_objZ);
        return 0;
    });
    s.At({0x0044E4F0, AddressOf(&GameNet::Manager::InNetGame)}, 0, [](Regs& r) -> uint32_t {
        Record({0x4E70, r.ecx});
        return 0x55;
    });
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) -> uint32_t {
        Record({0xDB60, Hash(reinterpret_cast<const char*>(r.args[0])), Hash(reinterpret_cast<const char*>(r.args[1]))});
        return 0;
    });
    s.At({0x0046D110, AddressOf(&FindChecksumNameOrUnknown)}, 0, [](Regs& r) -> uint32_t {
        Record({0x0A3E, r.args[0]});
        return reinterpret_cast<uint32_t>(s_nameText);
    });

    auto run = [&](bool original) {
        g_trace.clear();
        for (Saved& saved : g_csb_saved)
            std::memset(reinterpret_cast<void*>(saved.address), 0, saved.size);
        GameVariable<bool>(0x007CC979) = true;
        GameVariable<uint32_t>(0x00690FC8) = 0x12345;
        GameVariable<uint32_t>(0x00690FCC) = 0x6789;
        GameVariable<uint32_t>(0x00690FD0) = 0xABC;
        GameVariable<uint32_t>(0x00690FD4) = 0xDEF01;
        GameVariable<uint32_t>(0x00690FD8) = 0x2345;
        GameVariable<uint32_t>(0x00690FDC) = 0x678;
        GameVariable<uint32_t*>(0x006F3558) = s_cache;
        GameVariable<uint32_t>(0x007CCE14) = 0x00CE1400;
        ResetObjects();

        std::memset(table, 0, sizeof(table));
        std::memset(symbols, 0, sizeof(symbols));
        int used = 0;
        auto symbol = [&](uint32_t name, ESymbolType type, uint32_t raw) {
            CSymbolTableEntry* e = &symbols[used++];
            e->unknown0 = 1;
            e->type = type;
            e->name = name;
            e->raw = raw;
            LinkSymbol(e);
        };
        symbol(CF_REC, ESYMBOLTYPE_CFUNCTION, reinterpret_cast<uint32_t>(&CfRecord));
        symbol(CF_WAIT, ESYMBOLTYPE_CFUNCTION, reinterpret_cast<uint32_t>(&CfWait));
        symbol(CF_BLOCK, ESYMBOLTYPE_CFUNCTION, reinterpret_cast<uint32_t>(&CfBlock));
        symbol(CF_STOP, ESYMBOLTYPE_CFUNCTION, reinterpret_cast<uint32_t>(&CfStop));
        symbol(CF_ONEXIT, ESYMBOLTYPE_CFUNCTION, reinterpret_cast<uint32_t>(&CfOnExit));
        symbol(CF_INTERRUPT, ESYMBOLTYPE_CFUNCTION, reinterpret_cast<uint32_t>(&CfInterrupt));
        symbol(CF_END, ESYMBOLTYPE_CFUNCTION, reinterpret_cast<uint32_t>(&CfEnd));
        symbol(MF_ENTRY, ESYMBOLTYPE_MEMBERFUNCTION, reinterpret_cast<uint32_t>(&g_entryObj2));
        symbol(MF_NOENTRY, ESYMBOLTYPE_MEMBERFUNCTION, 0);
        symbol(MF_COMP, ESYMBOLTYPE_MEMBERFUNCTION, reinterpret_cast<uint32_t>(&g_entryComp));
        for (auto& c : g_scriptAt)
            symbol(c.first, ESYMBOLTYPE_QSCRIPT, 0x5C5C5C5C);
        symbol(ALIAS_SUB1, ESYMBOLTYPE_NAME, SUB1);

        uint8_t* arena = g_csb_arenas[original ? 0 : 1];
        std::memset(arena, 0, kArenaSize);
        std::memset(arena + 0x1000, 0xCD, kArenaSize - 0x1000);
        auto* manager = reinterpret_cast<Manager*>(arena);
        GameVariable<void*>(0x007D1800) = manager;
        auto* region = new (arena + 0x600) Region(arena + 0x1000, arena + 0x10000);
        auto* heap = new (arena + 0x620) Heap(region, 1, "default");
        auto* scriptRegion = new (arena + 0x6C0) Region(arena + 0x10000, arena + kArenaSize);
        auto* scriptHeap = new (arena + 0x6E0) Heap(scriptRegion, 1, "script");
        manager->m_heaps[0] = heap;
        manager->m_heaps[1] = scriptHeap;
        manager->m_heapCount = 2;
        manager->m_scriptHeap = scriptHeap;
        manager->PushContext(heap);
        reinterpret_cast<CExpressionEvaluator*>(0x006F2370)->Init();
        auto relocate = [&](uint32_t word) { return ArenaWord(g_csb_arenas, kArenaSize, word); };

        std::vector<OriginalCode*> guards;
        if (original) {
            for (auto& f : OurFunctions())
                guards.push_back(new OriginalCode(f.first));
        } else {
            for (auto& f : OurFunctions())
                RunPatch(f.first, Branch(0xE9, f.first, reinterpret_cast<void*>(f.second)));
        }

        auto newScript = [&](uint32_t name, const uint8_t* code, FakeObject* object) {
            void* memory = Mem::Malloc(sizeof(CScript));
            auto* script = GameFunction<CScript*(__thiscall*)(void*)>(0x004738E0)(memory); // Construct
            GameFunction<void(__thiscall*)(CScript*, uint32_t, const uint8_t*, CStruct*, Obj::CObject*)>(0x00474540)(
                script, name, code, nullptr, reinterpret_cast<Obj::CObject*>(object)); // SetScript
            return script;
        };
        auto update = [&](CScript* script) {
            return original ? Original<int(__thiscall*)(CScript*)>(0x004750B0)(script) : script->Update();
        };
        auto state = [&](CScript* script) {
            Record({PcOffset(script->m_pc), script->m_waitType, static_cast<uint32_t>(script->m_waitCounter),
                    script->m_flags, static_cast<uint32_t>(script->m_numReturnAddresses), script->m_scriptName,
                    reinterpret_cast<uint32_t>(script->m_currentLoop), reinterpret_cast<uint32_t>(script->m_loops),
                    reinterpret_cast<uint32_t>(CurrentScriptVar()), script->m_onExitScript,
                    reinterpret_cast<uint32_t>(script->m_object.Get())});
            for (int i = 0; i < script->m_numReturnAddresses; ++i) {
                const SReturnAddress& ra = script->m_returnAddresses[i];
                Record({ra.mScriptName, PcOffset(ra.mpReturnAddress), reinterpret_cast<uint32_t>(ra.mpLoop)});
            }
            Describe(script->m_params);
            Describe(script->m_functionParams);
        };

        for (MainScript& m : g_mains) {
            Record({0x3A3A3A3A, m.name});
            CScript* script = newScript(m.name, g_code.data() + m.at, m.object);
            for (int i = 0; i < 24; ++i) {
                int result = update(script);
                Record({0x0FDA7E, static_cast<uint32_t>(result)});
                state(script);
                if (result == ESCRIPTRETURNVAL_BLOCKED)
                    script->m_waitType = WAIT_TYPE_NONE; // (unblocked)
                if (result == ESCRIPTRETURNVAL_FINISHED)
                    break;
            }
        }

        // The member function calls with a null object (the script's own, or none), and the free function.
        {
            static const uint8_t line[] = {0x01, 0x24};
            CScript* script = newScript(0x3A000020, line, &g_objA);
            script->m_functionParams->AddInteger(P_TAG, 1);
            using EntryCall = bool(__thiscall*)(CScript*, CMemberFunctionEntry*, Obj::CObject*);
            using NameCall = bool(__thiscall*)(CScript*, uint32_t, Obj::CObject*);
            auto byEntry = [&](CMemberFunctionEntry* entry, FakeObject* object) {
                auto* o = reinterpret_cast<Obj::CObject*>(object);
                bool r = original ? Original<EntryCall>(0x004731A0)(script, entry, o) : script->CallMemberFunction(entry, o);
                Record({0xE0, r});
            };
            auto byName = [&](uint32_t name, FakeObject* object) {
                auto* o = reinterpret_cast<Obj::CObject*>(object);
                bool r = original ? Original<NameCall>(0x004731E0)(script, name, o) : script->CallMemberFunction(name, o);
                Record({0xE1, r});
            };
            auto free = [&](CMemberFunctionEntry* entry, FakeObject* object) {
                auto* o = reinterpret_cast<Obj::CObject*>(object);
                using FreeCall = bool(__cdecl*)(CMemberFunctionEntry*, CStruct*, Obj::CObject*, CScript*);
                bool r = original ? Original<FreeCall>(0x00472980)(entry, script->m_functionParams, o, script)
                                  : Script::CallMemberFunction(entry, script->m_functionParams, o, script);
                Record({0xE2, r});
            };
            byEntry(&g_entryObj, nullptr);
            byEntry(&g_entryComp, &g_objB);
            byName(MF_NAME, nullptr);
            byName(MF_NAME, &g_objB);
            free(&g_entryObj2, &g_objB);
            free(&g_entryComp, &g_objA);
            script->m_object.Release();
            byEntry(&g_entryObj, nullptr);
            byName(MF_NAME, nullptr);
            byEntry(&g_entryComp, &g_objA);
            byName(MF_NAME, &g_objA);
            // An interrupt of a script with no code that ends without ClearScript: Update clears its flag.
            GameFunction<void(__thiscall*)(CScript*)>(0x00473A80)(script); // ClearScript
            int r = GameFunction<int(__thiscall*)(CScript*, uint32_t, CStruct*)>(0x00475750)(script, INT_END, nullptr);
            Record({0xE3, static_cast<uint32_t>(r), script->m_flags});
            state(script);
        }

        for (OriginalCode* guard : guards)
            delete guard;
        EndRunPatches();
        for (FakeObject* o : {&g_objA, &g_objB, &g_objZ})
            Record({reinterpret_cast<uint32_t>(o->smartPointers)});

        std::vector<uint32_t> trace = std::move(g_trace);
        g_trace.clear();
        for (uint32_t& word : trace)
            word = relocate(word);
        std::vector<uint32_t> image(kArenaSize / 4);
        std::memcpy(image.data(), arena, kArenaSize);
        for (uint32_t& word : image) // (the flags word 0x02000001 once fell in the original's arena's range)
            word = ArenaWordOrHigh(g_csb_arenas, kArenaSize, word);
        trace.insert(trace.end(), image.begin(), image.end());
        GameVariable<void*>(0x007D1800) = nullptr;
        return trace;
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    RestoreCsB(); // (the stand-ins too)
    Compare(expected, actual, "script interpreter: Update and every statement token");
    Log("  script interpreter trace: %u words", static_cast<unsigned>(expected.size() - kArenaSize / 4));
}

} // namespace

SELFTEST(Early, TestScriptInterpreter, RestoreCsB);
