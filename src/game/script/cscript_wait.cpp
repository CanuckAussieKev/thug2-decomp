// CScript, group C: waiting, the call stack, interrupts and event handlers (0x004728F0-0x00475784).
//
// How these schedule a script (the spec for a later script runtime) is in docs/script-runtime.md; in short:
// a wait is set by a statement and counted by UpdateWait once per Update (the first time in the same Update);
// RunSubScript pushes the whole state of the running level and ReturnFromScript pops it; an interrupt is a call
// run at once by a nested Update, marked so that its return ends that Update.

#include <cstdint>

#include "game/script/cscript.h"

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/obj/event.h"
#include "game/obj/tracker.h"
#include "game/script/cache.h"
#include "game/script/parse.h"
#include "game/script/qb.h"
#include "game/script/struct.h"
#include "game/sys/game_clock.h"
#include "game/sys/timer.h"
#include "game/obj/control_pad.h"

namespace Scripts {

namespace {

// The game clock in ms (0x0044ECE0) and the offset added to it (0x00696D60).
int GameClock() { return FrameClock::GameClockElapsed(); }
int& GameClockOffset() { return GameObjects::g_clockOffset; }

// Wait n seconds: the milliseconds as seconds (0.001f, 0x3A83126F at 0x006461D8).
constexpr float kSecondsPerMs = 0.001f;

// The checksum of `exception` (an event handler table element that is an exception handler).
constexpr uint32_t kExceptionFlag = 0x80367192;

using GameObjects::CSignalRouteTable;

// A new event handler table (on the current heap).
CSignalRouteTable* NewEventHandlerTable()
{
    void* memory = MemSys::New(sizeof(CSignalRouteTable));
    return memory ? static_cast<CSignalRouteTable*>(memory)->Construct() : nullptr;
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
    case SUSPEND_ON_COUNT:
        if (m_waitCounter)
            --m_waitCounter;
        else
            m_waitType = SUSPEND_ON_NOTHING;
        break;
    case SUSPEND_ON_FRAME:
        if (m_waitCounter) {
            --m_waitCounter;
        } else if (!OnePerFrameReleased()) {
            // The first such script to finish counting this frame goes on; the others wait (at 0) for the next.
            OnePerFrameReleased() = true;
            m_waitType = SUSPEND_ON_NOTHING;
        }
        break;
    case SUSPEND_ON_TIMER: {
        double frame = FrameClock::GetSlomoFrameSeconds(); // (unrounded, as the original keeps it on the x87 stack)
        if (frame > m_waitTimer) {
            // Less than a frame left: done - unless this is the first Update, which only zeroes the time.
            if (m_waitTimerStarted)
                m_waitType = SUSPEND_ON_NOTHING;
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
            m_waitType = SUSPEND_ON_NOTHING;
        break;
    }
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
    case 10: {
        // The component decides (and stops the wait itself): vtable +0x18 (thiscall: the script, the argument).
        GameObjects::CObjectModule* component = m_waitComponent;
        auto check = reinterpret_cast<void(__thiscall*)(GameObjects::CObjectModule*, CScript*, uint32_t)>(
            (*reinterpret_cast<void***>(component))[0x18 / 4]);
        check(component, this, m_waitArg);
        break;
    }
    default:
        // SUSPEND_ON_BLOCK: until something unblocks it. None / unknown: nothing.
        break;
    }
}

// 0x004728F0
void CScript::StopWaiting() { m_waitType = SUSPEND_ON_NOTHING; }

// 0x00472900
bool CScript::IsBlocked(bool print)
{
    if (print)
        DebugOut::Printf("m_wait_type == %i\n" /* "m_wait_type == %i\n" */, m_waitType);
    return m_waitType == SUSPEND_ON_BLOCK;
}

// 0x00472BD0
void CScript::Wait(int frames)
{
    m_waitType = SUSPEND_ON_COUNT;
    m_waitCounter = frames;
}

// 0x00472BF0
void CScript::SuspendUntilNextFrame(int frames)
{
    m_waitType = SUSPEND_ON_FRAME;
    m_waitCounter = frames;
}

// 0x00472C10 (the milliseconds unsigned)
void CScript::WaitTime(int ms)
{
    m_waitType = SUSPEND_ON_TIMER;
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
    m_waitType = SUSPEND_ON_BLOCK;
    m_waitArg = eventType;
    if (eventType) {
        GameObjects::Tracker()->AddListener(eventType, this);
        m_spawnFlags |= SCRIPT_FLAG_TRACKER_BLOCKED;
    }
}

// 0x004739A0
void CScript::SetWaitOnComponent(EBlockReason type, GameObjects::CObjectModule* component, uint32_t arg)
{
    m_waitType = type;
    m_waitComponent = component;
    m_waitArg = arg;
    // (the component's flags +0x08: a script waits on it)
    *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(component) + 0x08) |= 2;
}

// 0x00472D20 (through ComponentDestroyedStub)
void CScript::ComponentDestroyed(GameObjects::CObjectModule* component)
{
    if (m_waitComponent && m_waitComponent == component)
        m_waitType = SUSPEND_ON_NOTHING;
    // The levels it's in the middle of: the saved wait type only (the component pointer stays).
    for (int i = 0; i < m_numReturnAddresses; ++i) {
        SCallFrame& frame = m_returnAddresses[i];
        if (frame.m_sleepComponent && frame.m_sleepComponent == component)
            frame.m_suspendKind &= ~RETURN_WAIT_TYPE_MASK;
    }
}

// 0x00472E70
void ComponentDestroyedAllScripts(GameObjects::CObjectModule* component)
{
    for (CScript* script = ScriptListHead(); script; script = script->m_following)
        script->ComponentDestroyed(component);
}

// 0x00473800
bool CScript::ReceiveEvent(GameObjects::CSignal* event)
{
    if (m_waitType == SUSPEND_ON_BLOCK && m_waitArg && event->m_type == m_waitArg)
        m_waitType = SUSPEND_ON_NOTHING;
    // Levels blocked on it further down go on when they're returned to.
    for (int i = 0; i < m_numReturnAddresses; ++i) {
        SCallFrame& frame = m_returnAddresses[i];
        if ((frame.m_suspendKind & RETURN_WAIT_TYPE_MASK) == SUSPEND_ON_BLOCK && frame.mWaitArg &&
            event->m_type == frame.mWaitArg)
            frame.m_suspendKind &= ~RETURN_WAIT_TYPE_MASK;
    }
    if (m_eventHandlers)
        m_eventHandlers->PassEvent(event, this);
    return true;
}

// 0x00472F40: from the top down, the first level entered as an interrupt (the top's flag is m_flags bit 0,
// a lower level's is its saved frame's bit 1): the level below it - the one it interrupted - stops waiting.
void CScript::UnblockInterruptedScript()
{
    bool interrupt = (m_flags & SCRIPT_RUN_INTERRUPTED) != 0;
    for (int level = m_numReturnAddresses; level > 0; --level) {
        SCallFrame& below = m_returnAddresses[level - 1];
        if (interrupt) {
            below.m_suspendKind &= ~RETURN_WAIT_TYPE_MASK;
            return;
        }
        interrupt = (below.mFlags & RETURN_FLAG_INTERRUPTED) != 0;
    }
}

// 0x004748E0
void CScript::RunSubScript(uint32_t name, const uint8_t* code, CStruct* params, GameObjects::CObject* object, bool interrupted)
{
    if (!m_pc) {
        // Nothing running: just start it.
        BindScript(name, code, params, object);
        m_flags = static_cast<uint8_t>((m_flags & ~SCRIPT_RUN_INTERRUPTED) | (interrupted ? SCRIPT_RUN_INTERRUPTED : 0));
        return;
    }
    // The third level: the call stack moves from inside the script to 16 on the script heap (no check beyond).
    if (m_returnAddresses == m_inlineReturnAddresses && m_numReturnAddresses == 2) {
        MemSys::EnterHeapScope(MemSys::Manager::Get()->m_scriptHeap);
        m_returnAddresses = static_cast<SCallFrame*>(MemSys::AllocBlock(16 * sizeof(SCallFrame)));
        MemSys::LeaveHeapScope();
        m_returnAddresses[0] = m_inlineReturnAddresses[0];
        m_returnAddresses[1] = m_inlineReturnAddresses[1];
    }
    // The caller's state (the frame's other wait-type and flag bits are left as they were).
    SCallFrame& frame = m_returnAddresses[m_numReturnAddresses++];
    frame.m_scriptId = m_scriptTitle;
    frame.m_argStruct = m_params;
    frame.m_callerFrame = m_pc;
    frame.m_ownerEntity = m_object.Get();
    frame.m_loopState = m_currentLoop;
    frame.mOnExitScript = m_onExitScript;
    frame.m_suspendKind = (frame.m_suspendKind & ~RETURN_WAIT_TYPE_MASK) | (m_waitType & RETURN_WAIT_TYPE_MASK);
    frame.m_sleepComponent = m_waitComponent;
    frame.mWaitArg = m_waitArg;
    frame.mWaitCounter = m_waitCounter;
    frame.m_sleepTimer = m_waitTimer;
    frame.mFlags = static_cast<uint8_t>((frame.mFlags & ~(RETURN_FLAG_TIMER_STARTED | RETURN_FLAG_INTERRUPTED)) |
                                        (m_waitTimerStarted ? RETURN_FLAG_TIMER_STARTED : 0) |
                                        ((m_flags & SCRIPT_RUN_INTERRUPTED) ? RETURN_FLAG_INTERRUPTED : 0));
    // The callee: its default parameters (its first line), then the call's. (With no code the original reads
    // through null - not checked.) The loop pointer is kept: the callee's loops go above the caller's.
    m_scriptTitle = name;
    m_pc = code;
    m_params = NewEmptyStruct();
    m_pc = ParseArguments(m_params, m_pc, nullptr);
    if (params)
        m_params->MergeStructure(params);
    m_object.Set(object);
    m_flags = static_cast<uint8_t>((m_flags & ~SCRIPT_RUN_INTERRUPTED) | (interrupted ? SCRIPT_RUN_INTERRUPTED : 0));
    m_waitType = SUSPEND_ON_NOTHING;
    m_onExitScript = 0;
}

// 0x00473E70
bool CScript::ReturnFromScript(CStruct* results)
{
    ScriptCache()->ReleaseScript(m_scriptTitle);
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
        SCallFrame& frame = m_returnAddresses[m_numReturnAddresses];
        m_scriptTitle = frame.m_scriptId;
        m_params = frame.m_argStruct;
        m_pc = frame.m_callerFrame;
        m_object.Set(frame.m_ownerEntity);
        m_currentLoop = frame.m_loopState;
        m_onExitScript = frame.mOnExitScript;
        m_waitType = frame.m_suspendKind & RETURN_WAIT_TYPE_MASK;
        // The rest of the wait only if it was waiting (otherwise the callee's values stay).
        if (m_waitType != SUSPEND_ON_NOTHING) {
            m_waitCounter = frame.mWaitCounter;
            m_waitTimer = frame.m_sleepTimer;
            m_waitTimerStarted = (frame.mFlags & RETURN_FLAG_TIMER_STARTED) != 0;
            m_waitComponent = frame.m_sleepComponent;
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
EScriptOutcome CScript::Interrupt(uint32_t name, CStruct* params)
{
    const uint8_t* code = ScriptCache()->GetScript(name);
    RunSubScript(name, code, params, m_object.Get(), true);
    return Update();
}

// 0x00472EA0
void CScript::ClearEventHandlerGroup(uint32_t group)
{
    if (m_eventHandlers)
        m_eventHandlers->RemoveGroup(this, group);
}

// 0x00472EC0
int CScript::PrintEventHandlerTable() { return CSignalRouteTable::PrintTable(m_eventHandlers); }

// 0x004734E0
void CScript::InstallEventTable(CArray* handlers, bool replace)
{
    MemSys::EnterHeapScope(MemSys::Manager::Get()->m_frontEndHeap);
    if (!m_eventHandlers)
        m_eventHandlers = NewEventHandlerTable();
    else
        m_eventHandlers->UnregisterWithTracker(this);
    m_eventHandlers->AddHandlers(handlers, replace);
    m_eventHandlers->Compact();
    m_eventHandlers->EnterWaitBoard(this);
    MemSys::LeaveHeapScope();
}

// 0x004735D0
void CScript::SetEventHandler(uint32_t eventType, uint32_t script, uint32_t group, bool exception, CStruct* params)
{
    MemSys::EnterHeapScope(MemSys::Manager::Get()->m_frontEndHeap);
    if (!m_eventHandlers)
        m_eventHandlers = NewEventHandlerTable();
    m_eventHandlers->AddHandler(this, eventType, script, group, exception, params);
    MemSys::LeaveHeapScope();
}

// 0x00473690
void CScript::ClearEventHandler(uint32_t eventType)
{
    if (m_eventHandlers) {
        m_eventHandlers->RemoveType(eventType);
        GameObjects::Tracker()->RemoveListener(eventType, this);
    }
}

// 0x004736C0: the group's handlers become those of `table`, an array of structures {flag, type, script,
// [params]} - the flag `exception` makes it an exception handler. (At least three components each: not checked.)
void CScript::ResetEventHandlersFromTable(CArray* table, uint32_t group)
{
    MemSys::EnterHeapScope(MemSys::Manager::Get()->m_frontEndHeap);
    if (m_eventHandlers)
        m_eventHandlers->MarkGroup(group);
    int count = static_cast<int>(table->m_size);
    if (!m_eventHandlers && count)
        m_eventHandlers = NewEventHandlerTable();
    uint32_t* elements = table->FindArrayValue();
    for (int i = 0; i < count; ++i) {
        CComponent* flag = reinterpret_cast<CStruct*>(elements[i])->StepToEntry(nullptr);
        CComponent* type = flag->next;
        CComponent* script = type->next;
        CComponent* params = script->next;
        m_eventHandlers->AddHandler(this, type->checksum, script->checksum, group, flag->checksum == kExceptionFlag,
                 params ? params->structure : nullptr);
    }
    // (the group's handlers not added again go)
    if (m_eventHandlers)
        m_eventHandlers->RemoveMarked(this);
    MemSys::LeaveHeapScope();
}

namespace {
void __cdecl ComponentDestroyedCdecl(CScript* script, GameObjects::CObjectModule* component)
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
