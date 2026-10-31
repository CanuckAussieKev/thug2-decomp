// The script interpreter: CScript::Update and the statements it runs (group B of the CScript batch; see
// cscript.h for the class and docs/script-runtime.md for the scheduling notes).
//
// How a script runs (the spec for a later port):
//
// Update (0x004750B0) runs tokens from m_pc until the script waits or ends:
//   - It first clears SCRIPT_RUN_WAIT_UPDATED, then loops while m_pc is set. Each pass: the script becomes the
//     current script (0x006F3530); unless SCRIPT_RUN_WAIT_UPDATED is set, UpdateWait counts the wait down /
//     checks it; if a wait is still on, Update sets SCRIPT_RUN_WAIT_UPDATED, CLEARS SCRIPT_RUN_INTERRUPTED,
//     clears the current script and returns BLOCKED (wait types 3 and 4) or WAITING (any other type).
//     Otherwise it runs ONE token or statement (below) and loops.
//   - SCRIPT_RUN_WAIT_UPDATED is only ever set by an Update that is returning because of a wait. Inside the loop
//     it is clear - except after a statement that ran a nested Update on the same script (an interrupt: the
//     '$' / return OnExitRun case, or a C function calling Interrupt) which ended waiting: the outer Update
//     then doesn't count the same wait a second time, sees it and returns too.
//   - A wait a statement sets is therefore checked at once, in the same Update: Wait n (frames) makes this
//     Update return WAITING with n-1 left; Wait 0 doesn't wait.
//   - m_pc == 0 (no code, or the script ended): FINISHED, with SCRIPT_RUN_INTERRUPTED cleared and no current
//     script.
//   - '$' / return of a level entered as an interrupt (ReturnFromScript says so): FINISHED_INTERRUPT (5) at
//     once - the current script is left set and SCRIPT_RUN_INTERRUPTED isn't cleared.
//
// The tokens Update handles itself (all others start a statement, run by ExecuteCommand):
//   01 end of line, 28, 3D endswitch: skipped (one byte)      02 end of line with line number: 5 bytes
//   20 begin, 21 repeat, 22 break: ExecuteBegin / ExecuteRepeat / ExecuteBreak
//   24 '$' end of script, 29 return [args]: this level's OnExitRun script (if any) is cleared and run as an
//      interrupt first (a nested Update), then ReturnFromScript ('$': no results; return: its arguments)
//   2E jump, 2F/37/40/41 random blocks, 49 short jump: ResolveRandomJumps
//   3C switch: ExecuteSwitch      47 if: ExecuteIf      48 else (reached at the end of an if's true part):
//      jump by its 2-byte offset (to the endif)
//   25, 26, 3E case, 3F default: nothing - m_pc doesn't move and Update loops for ever (these never start a
//      statement in compiled code; kept as in the original)
//
// A statement (ExecuteCommand) is a call or an assignment; its result is only used by `if`. A script call
// (CallScript) pushes the caller's state and runs the callee in the same Update loop, from its first token after
// the default-parameters line (the call counts as true). `obj:Name args` runs Name on the object called obj.

#include <cstdint>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/net/gamenet_manager.h"
#include "game/obj/object.h"
#include "game/script/cache.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/params.h"
#include "game/script/parse.h"
#include "game/script/qb.h"
#include "game/script/struct.h"
#include "game/gfx/loading_console.h"
#include "game/sys/log.h"

namespace Script {

namespace {

// The script whose missing calls aren't reported (presumably the default level's script).
constexpr uint32_t kQuietScriptName = 0xFB11E6CD;
// Member function entries for these component types are run by the object itself.
constexpr uint32_t kObjectComponentType = 0x57525413;
constexpr uint32_t kObjectComponentType2 = 0x0ADA4E74;

// The object's and its components' script member functions (virtual, not rewritten yet).
// CObject vtable +0x08: CallMemberFunction(entry, params, script); +0x0C: CallMemberFunction(name, params, script).
bool ObjectCallMemberFunction(Obj::CObject* object, CMemberFunctionEntry* entry, CStruct* params, CScript* script)
{
    auto* vtable = *reinterpret_cast<void***>(object);
    return reinterpret_cast<bool(__thiscall*)(Obj::CObject*, CMemberFunctionEntry*, CStruct*, CScript*)>(vtable[2])(
        object, entry, params, script);
}
bool ObjectCallMemberFunction(Obj::CObject* object, uint32_t name, CStruct* params, CScript* script)
{
    auto* vtable = *reinterpret_cast<void***>(object);
    return reinterpret_cast<bool(__thiscall*)(Obj::CObject*, uint32_t, CStruct*, CScript*)>(vtable[3])(
        object, name, params, script);
}
// CBaseComponent vtable +0x2C (the original tests the whole of EAX, not just AL).
bool ComponentCallMemberFunction(Obj::CBaseComponent* component, CMemberFunctionEntry* entry, CStruct* params,
                                 CScript* script)
{
    auto* vtable = *reinterpret_cast<void***>(component);
    return reinterpret_cast<uint32_t(__thiscall*)(Obj::CBaseComponent*, CMemberFunctionEntry*, CStruct*, CScript*)>(
               vtable[11])(component, entry, params, script) != 0;
}
// The object's component of `type` (CCompositeObject::GetComponent).
Obj::CBaseComponent* GetComponent(Obj::CObject* object, uint32_t type)
{
    return static_cast<Obj::CCompositeObject*>(object)->GetComponent(type);
}
// The object named `name` (Obj::FindObject: the object tracker's), or null.
Obj::CObject* FindObject(uint32_t name) { return Obj::FindObject(name); }
// 0x0044E4F0 on GameNet::Manager (0x007CCE14): called where a member function has no object to run on, its
// result unused - what is left of a compiled-out assert.
void NoObjectCheck() { GameNet::g_gameNetManager->InNetGame(); }

// A component made and freed around one expression / switch value.
void DeleteComponent(CComponent* component)
{
    CleanUpComponent(component);
    FreeComponent(component);
}

} // namespace

// 0x00472980: run a member function's table entry on `object` (no null checks).
bool CallMemberFunction(CMemberFunctionEntry* entry, CStruct* params, Obj::CObject* object, CScript* script)
{
    uint32_t type = entry->mComponentType;
    if (type == kObjectComponentType || type == kObjectComponentType2)
        return ObjectCallMemberFunction(object, entry, params, script);
    return ComponentCallMemberFunction(GetComponent(object, type), entry, params, script);
}

// 0x004731A0
bool CScript::CallMemberFunction(CMemberFunctionEntry* entry, Obj::CObject* object)
{
    if (!object)
        object = m_object.Get();
    if (!object) {
        NoObjectCheck();
        return false;
    }
    return Script::CallMemberFunction(entry, m_functionParams, object, this);
}

// 0x004731E0
bool CScript::CallMemberFunction(uint32_t name, Obj::CObject* object)
{
    if (!object)
        object = m_object.Get();
    if (!object) {
        NoObjectCheck();
        return false;
    }
    return ObjectCallMemberFunction(object, name, m_functionParams, this);
}

// 0x00472950
void CScript::ParseFunctionParams()
{
    m_functionParams->Clear();
    m_pc = ParseArguments(m_functionParams, m_pc, m_params);
}

// 0x004729E0: a name token, `<name>` (the parameter's checksum value) or `<...>` (the unnamed checksum
// parameter); anything else is 0 and isn't moved past.
uint32_t CScript::ReadName()
{
    uint32_t name = 0;
    switch (*m_pc) {
    case 0x16:
        ++m_pc;
        name = Read4Bytes(m_pc);
        m_pc += 4;
        break;
    case 0x2C:
        ++m_pc;
        m_params->GetChecksum(0u, &name, 0);
        break;
    case 0x2D: // 2D 16 <name>
        m_pc += 2;
        {
            uint32_t parameter = Read4Bytes(m_pc);
            m_pc += 4;
            m_params->GetChecksum(parameter, &name, 0);
        }
        break;
    }
    return name;
}

// 0x00474B40: one statement, at m_pc:
//   44 <name> args          a member function (without a table entry) on the script's object
//   46 <entry> args         a member function's table entry on the script's object
//   43 <function> args      a C function
//   ( expression )          a name result is the name to run (0: the name after it); any other result is the
//                           statement's value (non-zero: true) and nothing is run
//   name / <name> / <...>   then:
//     name : ...            on the object called `name`: 44 <name>, or a name (a script, or a member function -
//                           any other symbol does nothing). No such object: true, and m_pc isn't moved - the
//                           next pass runs the ':' as a statement, looking for the object named 0.
//     name = value          sets the script parameter `name` (the 4 bytes before the '=' are read as the name)
//     name args             a script (called: true), a C function, or a member function on the script's object;
//                           an unknown name is reported (except in the script 0xFB11E6CD) and is false
// The arguments are parsed into m_functionParams (they may refer to m_params).
bool CScript::ExecuteCommand()
{
    switch (*m_pc) {
    case 0x44: {
        ++m_pc;
        uint32_t name = Read4Bytes(m_pc);
        m_pc += 4;
        ParseFunctionParams();
        if (!m_object.Get()) {
            NoObjectCheck();
            return false;
        }
        return ObjectCallMemberFunction(m_object.Get(), name, m_functionParams, this);
    }
    case 0x46: {
        ++m_pc;
        auto* entry = reinterpret_cast<CMemberFunctionEntry*>(Read4Bytes(m_pc));
        m_pc += 4;
        ParseFunctionParams();
        if (!m_object.Get()) {
            NoObjectCheck();
            return false;
        }
        return Script::CallMemberFunction(entry, m_functionParams, m_object.Get(), this);
    }
    case 0x43: {
        ++m_pc;
        auto function = reinterpret_cast<CFunction>(Read4Bytes(m_pc));
        m_pc += 4;
        ParseFunctionParams();
        return function(m_functionParams, this);
    }
    }

    uint32_t name;
    if (*m_pc == 0x0E) {
        CComponent* value = NewComponent();
        m_pc = ParseExpression(m_pc, m_params, value);
        if (value->Type() != ESYMBOLTYPE_NAME) {
            bool result = value->raw != 0;
            DeleteComponent(value);
            return result;
        }
        name = value->checksum;
        DeleteComponent(value);
        if (!name)
            name = ReadName();
    } else {
        name = ReadName();
    }

    if (*m_pc == 0x42) { // name:
        Obj::CObject* object = FindObject(name);
        if (!object)
            return true; // (m_pc left at the ':')
        ++m_pc;
        m_pc = ResolveRandomJumps(m_pc);
        if (*m_pc == 0x44) {
            ++m_pc;
            uint32_t member = Read4Bytes(m_pc);
            m_pc += 4;
            ParseFunctionParams();
            return CallMemberFunction(member, object);
        }
        uint32_t function = ReadName();
        ParseFunctionParams();
        CSymbolTableEntry* symbol = LookUpSymbol(function);
        if (!symbol && m_scriptName == kQuietScriptName)
            return false;
        // (anywhere else a missing name's type is read through the null symbol: the original crashes - kept)
        switch (symbol->type) {
        case ESYMBOLTYPE_QSCRIPT: {
            const uint8_t* code = ScriptCache()->GetScript(symbol->name);
            CallScript(function, code, m_functionParams, object, false);
            return true;
        }
        case ESYMBOLTYPE_MEMBERFUNCTION:
            if (auto* entry = reinterpret_cast<CMemberFunctionEntry*>(symbol->raw))
                return CallMemberFunction(entry, object);
            return CallMemberFunction(function, object);
        default: // (C functions included: not run on an object)
            return false;
        }
    }

    if (*m_pc == 0x07) { // name = value
        m_pc -= 4;
        uint32_t target = Read4Bytes(m_pc);
        m_pc += 5;
        m_pc = ResolveRandomJumps(m_pc);
        CComponent* value = NewComponent();
        if (*m_pc == 0x0E)
            m_pc = ParseExpression(m_pc, m_params, value);
        else
            m_pc = ParseValue(m_pc, m_params, value);
        if (value->Type() != ESYMBOLTYPE_NONE) {
            value->name = target;
            m_params->AddComponent(value);
        } else {
            FreeComponent(value);
        }
        return true;
    }

    ParseFunctionParams();
    CSymbolTableEntry* symbol = LookUpSymbol(name);
    if (!symbol) {
        if (m_scriptName != kQuietScriptName)
            Dbg::Printf("WARNING: script %s not found, ignoring in default level.\n", FindChecksumNameOrUnknown(name));
        return false;
    }
    // Not original - the loading console (game/gfx/loading_console.h): while a loading screen is up, the scripts
    // called and the commands that load or build things.
    if (Gfx::LoadingConsole::Active() &&
        (symbol->type == ESYMBOLTYPE_QSCRIPT || symbol->type == ESYMBOLTYPE_CFUNCTION))
        Gfx::LoadingConsole::NoteScriptCall(name, symbol->type == ESYMBOLTYPE_CFUNCTION, m_functionParams);
    switch (symbol->type) {
    case ESYMBOLTYPE_QSCRIPT: {
        const uint8_t* code = ScriptCache()->GetScript(symbol->name);
        CallScript(name, code, m_functionParams, m_object.Get(), false);
        return true;
    }
    case ESYMBOLTYPE_CFUNCTION:
        return reinterpret_cast<CFunction>(symbol->raw)(m_functionParams, this);
    case ESYMBOLTYPE_MEMBERFUNCTION: {
        auto* entry = reinterpret_cast<CMemberFunctionEntry*>(symbol->raw);
        Obj::CObject* object = m_object.mp_object;
        if (entry) {
            if (object)
                return Script::CallMemberFunction(entry, m_functionParams, object, this);
        } else if (object) {
            return ObjectCallMemberFunction(object, name, m_functionParams, this);
        }
        NoObjectCheck();
        return false;
    }
    default:
        return false;
    }
}

// 0x00475050: 47 <offset to the else / endif> [39 not] condition. The condition is a statement (a script call
// in it runs first, and counts as true). If the script is still running and the (negated) result is false, it
// jumps; else it goes on into the true part.
void CScript::ExecuteIf()
{
    ++m_pc;
    const uint8_t* target = m_pc + Read2Bytes(m_pc);
    m_pc += 2;
    bool negate = false;
    if (*m_pc == 0x39) {
        ++m_pc;
        negate = true;
    }
    bool condition = ExecuteCommand();
    if (!m_pc)
        return;
    if (negate)
        condition = !condition;
    if (!condition)
        m_pc = target;
}

// 0x00472A70: m_pc just after a 3C switch: past its 3D endswitch (switches inside skipped whole).
void CScript::SkipToEndOfSwitch()
{
    while (*m_pc != 0x3D) {
        if (*m_pc == 0x3C) {
            ++m_pc;
            SkipToEndOfSwitch();
        } else {
            m_pc = SkipToken(m_pc);
        }
    }
    ++m_pc;
}

// 0x00473C60: 3C value ... then cases `3E 49 <offset to the next case> value ...`, `3F 49 <offset>` default
// and 3D endswitch. The switch's value is the first component of its line; a case matches if its line's first
// component equals it (ComponentsEqual). On a match, the case labels right after it (`49 xx xx 3E 49 xx xx
// value`) are skipped and the case's statements run from there; the 49 jump at the end of a case's statements
// goes to the endswitch. No match: on to the next case, finally into the default or past the endswitch.
void CScript::ExecuteSwitch()
{
    ++m_pc;
    ParseFunctionParams();
    CComponent* value = NewComponent();
    if (CComponent* first = m_functionParams->GetNextComponent(nullptr))
        CopyComponent(value, first, 0);
    for (;;) {
        uint8_t token = *m_pc;
        if (token == 0x24) // '$': not moved past - the original loops for ever here (kept)
            continue;
        if (token == 0x3C) { // a switch in a case not taken
            ++m_pc;
            SkipToEndOfSwitch();
            continue;
        }
        if (token == 0x3D) {
            ++m_pc;
            break;
        }
        if (token == 0x3F) { // default: past 3F 49 xx xx
            m_pc += 4;
            break;
        }
        if (token != 0x3E) {
            m_pc = SkipToken(m_pc);
            continue;
        }
        m_pc += 2; // the jump's offset
        const uint8_t* next = m_pc + Read2Bytes(m_pc);
        m_pc += 2;
        ParseFunctionParams();
        CComponent* match = m_functionParams->GetNextComponent(nullptr);
        if (!match || !ComponentsEqual(value, match)) {
            m_pc = next;
            continue;
        }
        m_pc = SkipEndOfLines(m_pc);
        while (*m_pc == 0x49 && m_pc[3] == 0x3E) {
            m_pc += 7;
            ParseFunctionParams();
            m_pc = SkipEndOfLines(m_pc);
        }
        break;
    }
    DeleteComponent(value);
}

// 0x00473220: 20 begin. The next loop of the loop stack starts after the token; the first nested loop (the
// inline loop in use) moves the stack to a block of 10 on the script heap - also when the inline loop belongs
// to a calling level (CallScript keeps m_currentLoop: one stack for every level), whose saved loop pointers are
// moved with it. Not checked beyond 10.
void CScript::ExecuteBegin()
{
    ++m_pc;
    if (m_currentLoop == &m_inlineLoop) {
        Mem::Manager* manager = Mem::Manager::Get();
        manager->PushContext(manager->m_scriptHeap);
        m_loops = static_cast<SLoop*>(Mem::Malloc(10 * sizeof(SLoop)));
        Mem::Manager::Get()->PopContext();
        m_loops[0] = m_inlineLoop;
        m_currentLoop = m_loops;
        for (int i = 0; i < m_numReturnAddresses; ++i) {
            if (SLoop* loop = m_returnAddresses[i].mpLoop)
                m_returnAddresses[i].mpLoop = m_loops + (loop - &m_inlineLoop);
        }
    }
    m_currentLoop = m_currentLoop ? m_currentLoop + 1 : m_loops;
    m_currentLoop->mpStart = m_pc;
    m_currentLoop->mpEnd = nullptr;
    m_currentLoop->mGotCount = false;
    m_currentLoop->mNeedToReadCount = true;
    m_currentLoop->mCount = 0;
}

// The loop below the current one (none: null).
static SLoop* OuterLoop(SLoop* loop, SLoop* loops) { return loop == loops ? nullptr : loop - 1; }

// 0x00472AB0: 21 repeat [count]. Its arguments are read the first time only (mpEnd is set past them; m_pc
// stays after the 21 - it only moves by jumping back or to mpEnd). No count: back to the start for ever (until
// a break). A count: counted down, back to the start until it reaches 0 - so `repeat 0` (or a negative count)
// goes round 2^32 times, as in the original.
void CScript::ExecuteRepeat()
{
    SLoop* loop = m_currentLoop;
    if (loop->mNeedToReadCount) {
        ++m_pc;
        m_functionParams->Clear();
        m_currentLoop->mpEnd = ParseArguments(m_functionParams, m_pc, m_params);
        m_currentLoop->mGotCount = m_functionParams->GetInteger(0u, &m_currentLoop->mCount, 0);
        m_currentLoop->mNeedToReadCount = false;
    }
    loop = m_currentLoop;
    if (!loop->mGotCount) {
        m_pc = loop->mpStart;
        return;
    }
    --loop->mCount;
    if (m_currentLoop->mCount != 0) {
        m_pc = m_currentLoop->mpStart;
        return;
    }
    m_pc = m_currentLoop->mpEnd;
    m_currentLoop = OuterLoop(m_currentLoop, m_loops);
}

// 0x00472B50: 22 break: past the repeat of this loop (counting nested begin / repeat), whose arguments are
// parsed (without the script's parameters) and dropped, then out of the loop.
void CScript::ExecuteBreak()
{
    int depth = 0;
    bool searching = true;
    do {
        uint8_t token = *m_pc;
        if (token == 0x20) {
            ++depth;
        } else if (token == 0x21) {
            if (depth)
                --depth;
            else
                searching = false;
        }
        m_pc = SkipToken(m_pc);
    } while (searching);
    m_functionParams->Clear();
    m_pc = ParseArguments(m_functionParams, m_pc, nullptr);
    m_currentLoop = OuterLoop(m_currentLoop, m_loops);
}

// 0x004750B0 (see the top of the file)
EScriptReturnVal CScript::Update()
{
    m_flags &= ~SCRIPT_RUN_WAIT_UPDATED;
    // (NOT ORIGINAL, log only: a script that runs this many steps in one Update never yields - a loop whose wait
    // ends at once, e.g. `while { Obj_WaitAnimFinished }` with no animation playing: the game hangs here, as the
    // original does. Logged once per script, with its name and its object's id, before the hang.)
    uint32_t steps = 0;
    while (m_pc) {
        if (++steps == 5000000u) {
            static uint32_t s_logged[16];
            static int s_count;
            bool seen = false;
            for (int i = 0; i < s_count; ++i)
                seen = seen || s_logged[i] == m_scriptName;
            if (!seen) {
                if (s_count < 16)
                    s_logged[s_count++] = m_scriptName;
                Obj::CObject* object = m_object.mp_object;
                // (the object's id as a number: FindChecksumNameOrUnknown's text for an unknown name is one buffer)
                Log("script: %s (object id %08X) has run %u steps in one frame without waiting - a loop that never "
                    "yields (the game hangs here)",
                    FindChecksumNameOrUnknown(m_scriptName), object ? object->m_id : 0u, steps);
            }
        }
        CurrentScriptVar() = this;
        if (!(m_flags & SCRIPT_RUN_WAIT_UPDATED))
            UpdateWait();
        if (m_waitType != WAIT_TYPE_NONE) {
            m_flags = static_cast<uint8_t>((m_flags & ~SCRIPT_RUN_INTERRUPTED) | SCRIPT_RUN_WAIT_UPDATED);
            CurrentScriptVar() = nullptr;
            return m_waitType == WAIT_TYPE_BLOCKED || m_waitType == WAIT_TYPE_REAL_TIMER ? ESCRIPTRETURNVAL_BLOCKED
                                                                                       : ESCRIPTRETURNVAL_WAITING;
        }
        switch (*m_pc) {
        case 0x01: // end of line
        case 0x28:
        case 0x3D: // endswitch
            ++m_pc;
            break;
        case 0x02: // end of line + line number
            m_pc += 5;
            break;
        case 0x20:
            ExecuteBegin();
            break;
        case 0x21:
            ExecuteRepeat();
            break;
        case 0x22:
            ExecuteBreak();
            break;
        case 0x24: // '$'
            if (uint32_t onExit = m_onExitScript) {
                m_onExitScript = 0;
                Interrupt(onExit, nullptr);
            }
            if (ReturnFromScript(nullptr))
                return ESCRIPTRETURNVAL_FINISHED_INTERRUPT;
            break;
        case 0x29: // return [args]
            if (uint32_t onExit = m_onExitScript) {
                m_onExitScript = 0;
                Interrupt(onExit, nullptr);
            }
            // (from wherever the on-exit script left m_pc - see docs/script-runtime.md)
            ++m_pc;
            ParseFunctionParams();
            if (ReturnFromScript(m_functionParams))
                return ESCRIPTRETURNVAL_FINISHED_INTERRUPT;
            break;
        case 0x25:
        case 0x26:
        case 0x3E: // case
        case 0x3F: // default
            break; // (not moved past: loops for ever, as in the original)
        case 0x2E:
        case 0x2F:
        case 0x37:
        case 0x40:
        case 0x41:
        case 0x49:
            m_pc = ResolveRandomJumps(m_pc);
            break;
        case 0x3C:
            ExecuteSwitch();
            break;
        case 0x47:
            ExecuteIf();
            break;
        case 0x48: // else: jump to the endif
            ++m_pc;
            m_pc += Read2Bytes(m_pc);
            break;
        default:
            ExecuteCommand();
            break;
        }
    }
    m_flags &= ~SCRIPT_RUN_INTERRUPTED;
    CurrentScriptVar() = nullptr;
    return ESCRIPTRETURNVAL_FINISHED;
}

} // namespace Script
