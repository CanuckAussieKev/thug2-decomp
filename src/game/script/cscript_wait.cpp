// CScript, group C: waiting, the call stack, interrupts and event handlers (0x004728F0-0x00475784).
//
// How these schedule a script (the spec for a later script runtime) is in docs/pending/csC.md; in short:
// a wait is set by a statement and counted by UpdateWait once per Update (the first time in the same Update);
// CallScript pushes the whole state of the running level and ReturnFromScript pops it; an interrupt is a call
// run at once by a nested Update, marked so that its return ends that Update.

#include "game/script/cscript.h"

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/script/cache.h"
#include "game/script/parse.h"
#include "game/script/qb.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Script {

namespace {

// The game clock in ms (0x0044ECE0) and the offset added to it (0x00696D60).
int GameClock() { return GameFunction<int(__cdecl*)()>(0x0044ECE0)(); }
int& GameClockOffset() { return GameVariable<int>(0x00696D60); }

// Wait n seconds: the milliseconds as seconds (0.001f, 0x3A83126F at 0x006461D8).
constexpr float kSecondsPerMs = 0.001f;

// The checksum of `exception` (an event handler table element that is an exception handler).
constexpr uint32_t kExceptionFlag = 0x80367192;

// An event's type (CEvent +0x00).
uint32_t EventType(const Obj::CEvent* event) { return *reinterpret_cast<const uint32_t*>(event); }

// The object tracker (0x006F0FC0): which scripts listen for which event types. Still the game's.
void* Tracker() { return GameVariable<void*>(0x006F0FC0); }
// 0x00463370: `script` waits for / handles events of type `type`.
void TrackerAddListener(uint32_t type, CScript* script)
{
    GameFunction<void(__thiscall*)(void*, uint32_t, CScript*)>(0x00463370)(Tracker(), type, script);
}
// 0x00463450: no longer.
void TrackerRemoveListener(uint32_t type, CScript* script)
{
    GameFunction<void(__thiscall*)(void*, uint32_t, CScript*)>(0x00463450)(Tracker(), type, script);
}

// The event handler table's methods (Obj::CEventHandlerTable, 0x10 bytes: the entries, their count, +0x08
// alive, +0x09 changed, +0x0C in use; an entry is 0x14 bytes: type, script, group, params, flags - bit 0
// exception, bit 1 marked for removal). Still the game's.
using Obj::CEventHandlerTable;
// 0x0045F820: the constructor (returns this).
CEventHandlerTable* ConstructTable(void* memory)
{
    return GameFunction<CEventHandlerTable*(__thiscall*)(void*)>(0x0045F820)(memory);
}
// 0x0045FA60: the tracker forgets the script's handlers' types. 0x0045FA10: the tracker is told them.
void TableUnregister(CEventHandlerTable* table, CScript* script)
{
    GameFunction<void(__thiscall*)(CEventHandlerTable*, CScript*)>(0x0045FA60)(table, script);
}
void TableRegister(CEventHandlerTable* table, CScript* script)
{
    GameFunction<void(__thiscall*)(CEventHandlerTable*, CScript*)>(0x0045FA10)(table, script);
}
// 0x004601A0: add an array of handler structures (`replace`: same types replaced; an empty array clears all).
void TableAddArray(CEventHandlerTable* table, CArray* handlers, bool replace)
{
    GameFunction<void(__thiscall*)(CEventHandlerTable*, CArray*, bool)>(0x004601A0)(table, handlers, replace);
}
// 0x0045FF50: the removed entries go.
void TableCompact(CEventHandlerTable* table)
{
    GameFunction<void(__thiscall*)(CEventHandlerTable*)>(0x0045FF50)(table);
}
// 0x0045FBC0: add (or replace the entry of) one handler.
void TableAdd(CEventHandlerTable* table, CScript* script, uint32_t type, uint32_t scriptName, uint32_t group,
              bool exception, CStruct* params)
{
    GameFunction<void(__thiscall*)(CEventHandlerTable*, CScript*, uint32_t, uint32_t, uint32_t, bool, CStruct*)>(
        0x0045FBC0)(table, script, type, scriptName, group, exception, params);
}
// 0x0045FDF0: remove the handlers of an event type.
void TableRemoveType(CEventHandlerTable* table, uint32_t type)
{
    GameFunction<void(__thiscall*)(CEventHandlerTable*, uint32_t)>(0x0045FDF0)(table, type);
}
// 0x004600E0: remove a group's handlers (0x8B713E0E: all).
void TableRemoveGroup(CEventHandlerTable* table, CScript* script, uint32_t group)
{
    GameFunction<void(__thiscall*)(CEventHandlerTable*, CScript*, uint32_t)>(0x004600E0)(table, script, group);
}
// 0x0045F840: mark a group's handlers for removal. 0x0045FED0: remove the marked ones.
void TableMarkGroup(CEventHandlerTable* table, uint32_t group)
{
    GameFunction<void(__thiscall*)(CEventHandlerTable*, uint32_t)>(0x0045F840)(table, group);
}
void TableRemoveMarked(CEventHandlerTable* table, CScript* script)
{
    GameFunction<void(__thiscall*)(CEventHandlerTable*, CScript*)>(0x0045FED0)(table, script);
}
// 0x00460470: run the handlers of `event` (as interrupts or spawned scripts).
void TablePassEvent(CEventHandlerTable* table, Obj::CEvent* event, CScript* script)
{
    GameFunction<void(__thiscall*)(CEventHandlerTable*, Obj::CEvent*, CScript*)>(0x00460470)(table, event, script);
}
// 0x0045F880 (cdecl): print the table (null: "Emtpy").
int PrintTable(CEventHandlerTable* table) { return GameFunction<int(__cdecl*)(CEventHandlerTable*)>(0x0045F880)(table); }

// A new event handler table (on the current heap).
CEventHandlerTable* NewEventHandlerTable()
{
    void* memory = Mem::New(0x10);
    return memory ? ConstructTable(memory) : nullptr;
}


// A new, empty structure.
CStruct* NewEmptyStruct()
{
    CStruct* structure = NewStruct();
    if (structure)
        structure->Init();
    return structure;
}

} // namespace

// 0x00473050
void CScript::UpdateWait()
{
    switch (m_waitType) {
    case WAIT_TYPE_COUNTER:
        if (m_waitCounter)
            --m_waitCounter;
        else
            m_waitType = WAIT_TYPE_NONE;
        break;
    case WAIT_TYPE_ONE_PER_FRAME:
        if (m_waitCounter) {
            --m_waitCounter;
        } else if (!OnePerFrameReleased()) {
            // The first such script to finish counting this frame goes on; the others wait (at 0) for the next.
            OnePerFrameReleased() = true;
            m_waitType = WAIT_TYPE_NONE;
        }
        break;
    case WAIT_TYPE_TIMER: {
        double frame = Tmr::GetSlomoFrameSeconds(); // (unrounded, as the original keeps it on the x87 stack)
        if (frame > m_waitTimer) {
            // Less than a frame left: done - unless this is the first Update, which only zeroes the time.
            if (m_waitTimerStarted)
                m_waitType = WAIT_TYPE_NONE;
            else
                m_waitTimer = 0.0f;
        } else {
            // (also when either is a NaN)
            m_waitTimer = static_cast<float>(m_waitTimer - frame);
        }
        m_waitTimerStarted = true;
        break;
    }
    case WAIT_TYPE_REAL_TIMER: {
        uint32_t start = m_waitArg;
        uint32_t elapsed = static_cast<uint32_t>(GameClock()) + (static_cast<uint32_t>(GameClockOffset()) - start);
        if (static_cast<double>(elapsed) >= m_waitTimer) // (not when the time is a NaN)
            m_waitType = WAIT_TYPE_NONE;
        break;
    }
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
    case 10: {
        // The component decides (and stops the wait itself): vtable +0x18 (thiscall: the script, the argument).
        Obj::CBaseComponent* component = m_waitComponent;
        auto check = reinterpret_cast<void(__thiscall*)(Obj::CBaseComponent*, CScript*, uint32_t)>(
            (*reinterpret_cast<void***>(component))[0x18 / 4]);
        check(component, this, m_waitArg);
        break;
    }
    default:
        // WAIT_TYPE_BLOCKED: until something unblocks it. None / unknown: nothing.
        break;
    }
}

// 0x004728F0
void CScript::StopWaiting() { m_waitType = WAIT_TYPE_NONE; }

// 0x00472900
bool CScript::IsBlocked(bool print)
{
    if (print)
        Dbg::Printf(reinterpret_cast<const char*>(0x00648410) /* "m_wait_type == %i\n" */, m_waitType);
    return m_waitType == WAIT_TYPE_BLOCKED;
}

// 0x00472BD0
void CScript::Wait(int frames)
{
    m_waitType = WAIT_TYPE_COUNTER;
    m_waitCounter = frames;
}

// 0x00472BF0
void CScript::WaitOnePerFrame(int frames)
{
    m_waitType = WAIT_TYPE_ONE_PER_FRAME;
    m_waitCounter = frames;
}

// 0x00472C10 (the milliseconds unsigned)
void CScript::WaitTime(int ms)
{
    m_waitType = WAIT_TYPE_TIMER;
    m_waitTimerStarted = false;
    m_waitTimer = static_cast<float>(static_cast<double>(static_cast<uint32_t>(ms)) * kSecondsPerMs);
}

// 0x00473310 (the milliseconds unsigned)
void CScript::WaitRealTime(int ms)
{
    m_waitType = WAIT_TYPE_REAL_TIMER;
    m_waitArg = static_cast<uint32_t>(GameClock() + GameClockOffset());
    m_waitTimer = static_cast<float>(static_cast<uint32_t>(ms));
}

// 0x00472FD0
void CScript::Block(uint32_t eventType)
{
    m_waitType = WAIT_TYPE_BLOCKED;
    m_waitArg = eventType;
    if (eventType) {
        TrackerAddListener(eventType, this);
        m_spawnFlags |= SCRIPT_FLAG_TRACKER_BLOCKED;
    }
}

// 0x004739A0
void CScript::SetWaitOnComponent(EWaitType type, Obj::CBaseComponent* component, uint32_t arg)
{
    m_waitType = type;
    m_waitComponent = component;
    m_waitArg = arg;
    // (the component's flags +0x08: a script waits on it)
    *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(component) + 0x08) |= 2;
}

// 0x00472D20 (through ComponentDestroyedStub)
void CScript::ComponentDestroyed(Obj::CBaseComponent* component)
{
    if (m_waitComponent && m_waitComponent == component)
        m_waitType = WAIT_TYPE_NONE;
    // The levels it's in the middle of: the saved wait type only (the component pointer stays).
    for (int i = 0; i < m_numReturnAddresses; ++i) {
        SReturnAddress& frame = m_returnAddresses[i];
        if (frame.mpWaitComponent && frame.mpWaitComponent == component)
            frame.mWaitType &= ~RETURN_WAIT_TYPE_MASK;
    }
}

// 0x00472E70
void ComponentDestroyedAllScripts(Obj::CBaseComponent* component)
{
    for (CScript* script = ScriptListHead(); script; script = script->mp_next)
        script->ComponentDestroyed(component);
}

// 0x00473800
bool CScript::ReceiveEvent(Obj::CEvent* event)
{
    if (m_waitType == WAIT_TYPE_BLOCKED && m_waitArg && EventType(event) == m_waitArg)
        m_waitType = WAIT_TYPE_NONE;
    // Levels blocked on it further down go on when they're returned to.
    for (int i = 0; i < m_numReturnAddresses; ++i) {
        SReturnAddress& frame = m_returnAddresses[i];
        if ((frame.mWaitType & RETURN_WAIT_TYPE_MASK) == WAIT_TYPE_BLOCKED && frame.mWaitArg &&
            EventType(event) == frame.mWaitArg)
            frame.mWaitType &= ~RETURN_WAIT_TYPE_MASK;
    }
    if (m_eventHandlers)
        TablePassEvent(m_eventHandlers, event, this);
    return true;
}

// 0x00472F40: from the top down, the first level entered as an interrupt (the top's flag is m_flags bit 0,
// a lower level's is its saved frame's bit 1): the level below it - the one it interrupted - stops waiting.
void CScript::UnblockInterruptedScript()
{
    bool interrupt = (m_flags & SCRIPT_RUN_INTERRUPTED) != 0;
    for (int level = m_numReturnAddresses; level > 0; --level) {
        SReturnAddress& below = m_returnAddresses[level - 1];
        if (interrupt) {
            below.mWaitType &= ~RETURN_WAIT_TYPE_MASK;
            return;
        }
        interrupt = (below.mFlags & RETURN_FLAG_INTERRUPTED) != 0;
    }
}

// 0x004748E0
void CScript::CallScript(uint32_t name, const uint8_t* code, CStruct* params, Obj::CObject* object, bool interrupted)
{
    if (!m_pc) {
        // Nothing running: just start it.
        SetScript(name, code, params, object);
        m_flags = static_cast<uint8_t>((m_flags & ~SCRIPT_RUN_INTERRUPTED) | (interrupted ? SCRIPT_RUN_INTERRUPTED : 0));
        return;
    }
    // The third level: the call stack moves from inside the script to 16 on the script heap (no check beyond).
    if (m_returnAddresses == m_inlineReturnAddresses && m_numReturnAddresses == 2) {
        Mem::PushContext(Mem::Manager::Get()->m_scriptHeap);
        m_returnAddresses = static_cast<SReturnAddress*>(Mem::Malloc(16 * sizeof(SReturnAddress)));
        Mem::PopContext();
        m_returnAddresses[0] = m_inlineReturnAddresses[0];
        m_returnAddresses[1] = m_inlineReturnAddresses[1];
    }
    // The caller's state (the frame's other wait-type and flag bits are left as they were).
    SReturnAddress& frame = m_returnAddresses[m_numReturnAddresses++];
    frame.mScriptName = m_scriptName;
    frame.mpParams = m_params;
    frame.mpReturnAddress = m_pc;
    frame.mpObject = m_object.Get();
    frame.mpLoop = m_currentLoop;
    frame.mOnExitScript = m_onExitScript;
    frame.mWaitType = (frame.mWaitType & ~RETURN_WAIT_TYPE_MASK) | (m_waitType & RETURN_WAIT_TYPE_MASK);
    frame.mpWaitComponent = m_waitComponent;
    frame.mWaitArg = m_waitArg;
    frame.mWaitCounter = m_waitCounter;
    frame.mWaitTimer = m_waitTimer;
    frame.mFlags = static_cast<uint8_t>((frame.mFlags & ~(RETURN_FLAG_TIMER_STARTED | RETURN_FLAG_INTERRUPTED)) |
                                        (m_waitTimerStarted ? RETURN_FLAG_TIMER_STARTED : 0) |
                                        ((m_flags & SCRIPT_RUN_INTERRUPTED) ? RETURN_FLAG_INTERRUPTED : 0));
    // The callee: its default parameters (its first line), then the call's. (With no code the original reads
    // through null - not checked.) The loop pointer is kept: the callee's loops go above the caller's.
    m_scriptName = name;
    m_pc = code;
    m_params = NewEmptyStruct();
    m_pc = ParseArguments(m_params, m_pc, nullptr);
    if (params)
        m_params->AppendStructure(params);
    m_object.Set(object);
    m_flags = static_cast<uint8_t>((m_flags & ~SCRIPT_RUN_INTERRUPTED) | (interrupted ? SCRIPT_RUN_INTERRUPTED : 0));
    m_waitType = WAIT_TYPE_NONE;
    m_onExitScript = 0;
}

// 0x00473E70
bool CScript::ReturnFromScript(CStruct* results)
{
    ScriptCache()->ReleaseScript(m_scriptName);
    bool interrupted = (m_flags & SCRIPT_RUN_INTERRUPTED) != 0;
    if (m_numReturnAddresses == 0) {
        // The bottom level: the script ends (its parameters stay, for the results).
        m_pc = nullptr;
    } else {
        --m_numReturnAddresses;
        if (m_params) {
            m_params->Clear();
            DeleteStruct(m_params);
        }
        SReturnAddress& frame = m_returnAddresses[m_numReturnAddresses];
        m_scriptName = frame.mScriptName;
        m_params = frame.mpParams;
        m_pc = frame.mpReturnAddress;
        m_object.Set(frame.mpObject);
        m_currentLoop = frame.mpLoop;
        m_onExitScript = frame.mOnExitScript;
        m_waitType = frame.mWaitType & RETURN_WAIT_TYPE_MASK;
        // The rest of the wait only if it was waiting (otherwise the callee's values stay).
        if (m_waitType != WAIT_TYPE_NONE) {
            m_waitCounter = frame.mWaitCounter;
            m_waitTimer = frame.mWaitTimer;
            m_waitTimerStarted = (frame.mFlags & RETURN_FLAG_TIMER_STARTED) != 0;
            m_waitComponent = frame.mpWaitComponent;
            m_waitArg = frame.mWaitArg;
        }
        m_flags = static_cast<uint8_t>((m_flags & ~SCRIPT_RUN_INTERRUPTED) |
                                       ((frame.mFlags & RETURN_FLAG_INTERRUPTED) ? SCRIPT_RUN_INTERRUPTED : 0));
    }
    if (results)
        m_params->AppendStructureIfAny(results);
    return interrupted;
}

// 0x00475750
EScriptReturnVal CScript::Interrupt(uint32_t name, CStruct* params)
{
    const uint8_t* code = ScriptCache()->GetScript(name);
    CallScript(name, code, params, m_object.Get(), true);
    return Update();
}

// 0x00472EA0
void CScript::ClearEventHandlerGroup(uint32_t group)
{
    if (m_eventHandlers)
        TableRemoveGroup(m_eventHandlers, this, group);
}

// 0x00472EC0
int CScript::PrintEventHandlerTable() { return PrintTable(m_eventHandlers); }

// 0x004734E0
void CScript::SetEventHandlers(CArray* handlers, bool replace)
{
    Mem::PushContext(Mem::Manager::Get()->m_frontEndHeap);
    if (!m_eventHandlers)
        m_eventHandlers = NewEventHandlerTable();
    else
        TableUnregister(m_eventHandlers, this);
    TableAddArray(m_eventHandlers, handlers, replace);
    TableCompact(m_eventHandlers);
    TableRegister(m_eventHandlers, this);
    Mem::PopContext();
}

// 0x004735D0
void CScript::SetEventHandler(uint32_t eventType, uint32_t script, uint32_t group, bool exception, CStruct* params)
{
    Mem::PushContext(Mem::Manager::Get()->m_frontEndHeap);
    if (!m_eventHandlers)
        m_eventHandlers = NewEventHandlerTable();
    TableAdd(m_eventHandlers, this, eventType, script, group, exception, params);
    Mem::PopContext();
}

// 0x00473690
void CScript::ClearEventHandler(uint32_t eventType)
{
    if (m_eventHandlers) {
        TableRemoveType(m_eventHandlers, eventType);
        TrackerRemoveListener(eventType, this);
    }
}

// 0x004736C0: the group's handlers become those of `table`, an array of structures {flag, type, script,
// [params]} - the flag `exception` makes it an exception handler. (At least three components each: not checked.)
void CScript::ResetEventHandlersFromTable(CArray* table, uint32_t group)
{
    Mem::PushContext(Mem::Manager::Get()->m_frontEndHeap);
    if (m_eventHandlers)
        TableMarkGroup(m_eventHandlers, group);
    int count = static_cast<int>(table->m_size);
    if (!m_eventHandlers && count)
        m_eventHandlers = NewEventHandlerTable();
    uint32_t* elements = table->GetArrayPointer();
    for (int i = 0; i < count; ++i) {
        CComponent* flag = reinterpret_cast<CStruct*>(elements[i])->GetNextComponent(nullptr);
        CComponent* type = flag->next;
        CComponent* script = type->next;
        CComponent* params = script->next;
        TableAdd(m_eventHandlers, this, type->checksum, script->checksum, group, flag->checksum == kExceptionFlag,
                 params ? params->structure : nullptr);
    }
    // (the group's handlers not added again go)
    if (m_eventHandlers)
        TableRemoveMarked(m_eventHandlers, this);
    Mem::PopContext();
}

namespace {
void __cdecl ComponentDestroyedCdecl(CScript* script, Obj::CBaseComponent* component)
{
    script->ComponentDestroyed(component);
}
} // namespace

// 0x00472D20: thiscall, RET 4, ECX kept.
__declspec(naked) void ComponentDestroyedStub()
{
    __asm {
        push ecx
        push dword ptr [esp + 8]
        push ecx
        call ComponentDestroyedCdecl
        add esp, 8
        pop ecx
        ret 4
    }
}

} // namespace Script

REPLACE(0x00473050, &Script::CScript::UpdateWait);
REPLACE(0x004728F0, &Script::CScript::StopWaiting);
REPLACE(0x00472900, &Script::CScript::IsBlocked);
REPLACE(0x00472BD0, &Script::CScript::Wait);
REPLACE(0x00472BF0, &Script::CScript::WaitOnePerFrame);
REPLACE(0x00472C10, &Script::CScript::WaitTime);
REPLACE(0x00473310, &Script::CScript::WaitRealTime);
REPLACE(0x00472FD0, &Script::CScript::Block);
REPLACE(0x004739A0, &Script::CScript::SetWaitOnComponent);
REPLACE_UNCHECKED(0x00472D20, Script::ComponentDestroyedStub); // ECX kept for 0x00472E70 (see the header)
REPLACE(0x00472E70, Script::ComponentDestroyedAllScripts);
REPLACE(0x00473800, &Script::CScript::ReceiveEvent);
REPLACE(0x00472F40, &Script::CScript::UnblockInterruptedScript);
REPLACE(0x004748E0, &Script::CScript::CallScript);
REPLACE(0x00473E70, &Script::CScript::ReturnFromScript);
REPLACE(0x00475750, &Script::CScript::Interrupt);
REPLACE(0x00472EA0, &Script::CScript::ClearEventHandlerGroup);
REPLACE(0x00472EC0, &Script::CScript::PrintEventHandlerTable);
REPLACE(0x004734E0, &Script::CScript::SetEventHandlers);
REPLACE(0x004735D0, &Script::CScript::SetEventHandler);
REPLACE(0x00473690, &Script::CScript::ClearEventHandler);
REPLACE(0x004736C0, &Script::CScript::ResetEventHandlersFromTable);
