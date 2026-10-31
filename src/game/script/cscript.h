#pragma once

#include <cstddef>
#include <cstdint>
#include <new>

#include "game/obj/smtptr.h"
#include "game/script/param_struct.h"
#include "game/core/slab_bank.h"

// A running script (Scripts::CScript, 0xF4 bytes, pool 0x006F3548 of 300 - ours 1200, game/core/memory_sizes.h) and the list of all of them.
//
// A script runs compiled .qb code (see qb.h) one statement at a time from its program counter until it has to
// wait or it ends; Update does that once (the callers decide when: the spawned scripts every frame, objects'
// scripts from their own updates, LaunchScript all at once). Calling another script pushes a return address
// (the caller's whole state, 0x30 bytes) and runs the callee in the same Update; '$' / return pops it.
// Every script is on one list (head 0x006F3538, the newest first). Spawned scripts belong to the list and are
// updated and deleted by TickChildScripts; the others belong to whoever made them (objects...).
//
// Layout and conventions are from the disassembly of 0x004728F0-0x00475920 (docs/script-runtime.md).
// Every method is __thiscall, callee-pops (RET n = 4 * arguments); the free functions are __cdecl. None takes
// arguments in other registers. One caller relies on ECX surviving a call: 0x00472E70 calls 0x00472D20 and
// reads ECX afterwards (both must be ours together, or 0x00472D20 must preserve ECX - it does in the original).
namespace GameObjects {
class CEntity;          // a game object (CSharedBase; +0x100 its component list)
class CObjectModule;   // one of an object's components; can hold a script waiting on it
class CSignalRouteTable; // a script's event handlers (game/obj/signal.h)
struct CSignal;          // an event (game/obj/signal.h)
} // namespace Obj

namespace Scripts {

class CScript;

// What Update returns.
enum EScriptOutcome : int {
    SCRIPT_OUTCOME_FINISHED = 0,           // no code left (the script has ended or was never set)
    SCRIPT_OUTCOME_BLOCKED = 1,            // waiting with SUSPEND_ON_BLOCK or WAIT_TYPE_REAL_TIMER
    SCRIPT_OUTCOME_SUSPENDED = 2,            // any other wait
    // (3 and 4 are never returned by this build)
    SCRIPT_OUTCOME_ENDED_BY_INTERRUPT = 5, // an interrupt (Interrupt / OnExitRun script) returned
};

// m_waitType: what the script waits for before its next statement (checked by UpdateWait, 0x00473050).
enum EBlockReason : uint32_t {
    SUSPEND_ON_NOTHING = 0,
    SUSPEND_ON_COUNT = 1,        // m_waitCounter more Updates (Wait n frames: 0x00472BD0)
    SUSPEND_ON_TIMER = 2,          // m_waitTimer seconds of slomo frame time (Wait n seconds: 0x00472C10)
    SUSPEND_ON_BLOCK = 3,        // until unblocked - by an event of type m_waitArg (Block: 0x00472FD0) or by hand
    WAIT_TYPE_REAL_TIMER = 4,     // m_waitTimer ms of the game clock since m_waitArg (0x00473310)
    // 5-10: waiting on m_waitComponent, which is asked every Update (its vtable +0x18) - the object waits
    // (move, anim, jump, stop, rotate, stream finished...; set by SetWaitOnComponent 0x004739A0).
    WAIT_TYPE_OBJECT_FIRST = 5,
    WAIT_TYPE_OBJECT_LAST = 10,
    SUSPEND_ON_FRAME = 11, // like COUNTER, but at 0 only one such script per frame goes on (0x006F352C)
};

// A begin ... repeat loop (0x10 bytes). One is inside the script; the second nested loop moves them all to a
// block of 10 on the script heap (no check beyond 10).
struct SRepeatFrame {
    const uint8_t* m_tokensBegin;  // the first token after 'begin'
    const uint8_t* m_tokensEnd;    // the token after 'repeat n' (set the first time repeat is reached)
    bool m_argsSeen;          // repeat had a count (otherwise the loop is endless)
    bool m_argsExpected;   // repeat's arguments not read yet
    uint16_t unknownA;
    int m_headcount;              // repeats left
};
static_assert(sizeof(SRepeatFrame) == 0x10, "layout must match the original");
static_assert(offsetof(SRepeatFrame, m_tokensEnd) == 0x4 && offsetof(SRepeatFrame, m_argsSeen) == 0x8 &&
                  offsetof(SRepeatFrame, m_argsExpected) == 0x9 && offsetof(SRepeatFrame, m_headcount) == 0xC,
              "layout must match the original");

// A return address (0x30 bytes): the calling level's state, saved by RunSubScript and restored by
// ReturnFromScript. Two are inside the script; the third call moves them to a block of 16 on the script
// heap (no check beyond 16).
struct SCallFrame {
    uint32_t m_scriptId;        // +0x00 m_scriptTitle
    CStruct* m_argStruct;           // +0x04 m_args (owned)
    const uint8_t* m_callerFrame; // +0x08 m_pc
    GameObjects::CEntity* m_ownerEntity;      // +0x0C m_entity (a plain pointer here: ObjectDestroyed clears it)
    SRepeatFrame* m_loopState;               // +0x10 m_currentLoop
    uint32_t mOnExitScript;      // +0x14 m_onExitScript
    GameObjects::CObjectModule* m_sleepComponent; // +0x18 m_waitComponent
    float m_sleepTimer;            // +0x1C m_waitTimer
    uint32_t mWaitArg;           // +0x20 m_waitArg
    int mWaitCounter;            // +0x24 m_waitCounter
    uint32_t m_suspendKind;          // +0x28 bits 0-3: m_waitType (the other bits are kept as they are)
    uint8_t m_traitMask;              // +0x2C bit 0: m_waitTimerStarted, bit 1: the level was an interrupt
    uint8_t unknown2D[3];
};
static_assert(sizeof(SCallFrame) == 0x30, "layout must match the original");
static_assert(offsetof(SCallFrame, m_argStruct) == 0x04 && offsetof(SCallFrame, m_callerFrame) == 0x08 &&
                  offsetof(SCallFrame, m_ownerEntity) == 0x0C && offsetof(SCallFrame, m_loopState) == 0x10 &&
                  offsetof(SCallFrame, mOnExitScript) == 0x14 &&
                  offsetof(SCallFrame, m_sleepComponent) == 0x18 && offsetof(SCallFrame, m_sleepTimer) == 0x1C &&
                  offsetof(SCallFrame, mWaitArg) == 0x20 && offsetof(SCallFrame, mWaitCounter) == 0x24 &&
                  offsetof(SCallFrame, m_suspendKind) == 0x28 && offsetof(SCallFrame, m_traitMask) == 0x2C,
              "layout must match the original");
constexpr uint32_t RETURN_WAIT_TYPE_MASK = 0xF;
constexpr uint8_t RETURN_FLAG_TIMER_STARTED = 0x01;
constexpr uint8_t RETURN_FLAG_INTERRUPTED = 0x02;

// A script held by a structure component (8 bytes, filled by CStruct's 0x00476900): its name and its code.
// (Group A: SetScriptCopy's argument.)
struct SParamScript {
    uint32_t m_ownNameHash;
    const uint8_t* m_codeStream;
};
static_assert(sizeof(SParamScript) == 8, "allocated as 8 bytes by 0x0048B200");

// A member function's table entry (the value of a QBVAL_NATIVE_METHOD symbol, token 0x46), 0x18 bytes: the
// tables at 0x00683550 (CEntity's, filled in by 0x00642940) and 0x00683268 (CGameEntity's, 0x006425D0).
struct CMemberFunctionEntry {
    uint32_t m_callsign;
    uint32_t mComponentType; // the object component that runs it; 0x57525413 / 0x0ADA4E74: the object itself
    // +0x08: the member function, as a 16-byte (virtual-inheritance) member function pointer, `bool (Class::*)(
    // CStruct* params, CScript* script)`; null: the object looks the name up (CEntity::InvokeMethodByName 0x0045D850).
    void* mCode;
    int mThisAdjustment;      // +0x0C
    int mVirtualBaseOffsets[2]; // +0x10 (unused by single-inheritance classes)
};
static_assert(sizeof(CMemberFunctionEntry) == 0x18, "a table entry is 0x18 bytes");

class CScript {
public:
    // --- life (group A: cscript_core.cpp) ---
    // 0x004738E0: the constructor (returns this). Zeroes the whole object (after constructing m_refCounted:
    // its vtable ends up null - kept), takes the next unique id, goes to the head of the list.
    CScript* MakeNew();
    // 0x00474420: the destructor body: ResetScriptState, ReportStopped, frees the callback parameters, leaves the
    // list, forgets its stored randoms, lets go of the event handler table, ~m_refCounted, ~m_entity.
    void Teardown();
    // 0x00449970 / 0x004499B0 (cdecl): CPoolable new / delete (the pool 0x006F3548, else the current heap).
    static void* operator new(size_t size);
    static void operator delete(void* script);

    // 0x00473A80: stop: run the OnExitRun scripts, free the code copy, release the code and every level's
    // code from the script cache, free the parameters and every level's, back to the inline call stack /
    // loop, no wait, no object, no name, interrupt flags cleared.
    void ResetScriptState();
    // 0x00473880: run the pending OnExitRun scripts (this level's, then each return address' from the top) as
    // interrupts. Also called by 0x00583470.
    void RunOnExitScripts();
    // 0x00473010: after ResetScriptState: the tracker forgets the event handler table's types for this script and
    // every handler is removed (group all_groups); with SCRIPT_FLAG_TRACKER_BLOCKED, the tracker (0x006F0FC0,
    // 0x00462B90) forgets it.
    void ReportStopped();
    // 0x004739D0: a copy of `params` if it is (or is inside) this script's own parameters or function
    // parameters, which ResetScriptState is about to free; else null.
    CStruct* CopyParamsIfOwned(CStruct* params);
    // 0x00474540: run `name` from the start: `code`, or the cached code of `name` (if there is none: stop and
    // ReportStopped). The parameters are the script's defaults (its first line) plus `params`.
    void BindScript(uint32_t name, const uint8_t* code, CStruct* params, GameObjects::CEntity* object);
    // 0x004747A0 / 0x004747C0: BindScript(name, null, params, object), the name as a checksum or a string.
    void BindScript(uint32_t name, CStruct* params, GameObjects::CEntity* object);
    void BindScript(const char* name, CStruct* params, GameObjects::CEntity* object);
    // 0x00474720: BindScript with a private copy of `script`'s code (m_codeCopy, on the script heap) - a script
    // held in a structure (0x0048B200 gets it with 0x00476900).
    void SetScriptCopy(SParamScript* script, CStruct* params, GameObjects::CEntity* object);
    // 0x004747F0: start again the script it was started with (the bottom level's name, parameters, object).
    void Relaunch();
    // 0x00472C90: whether the scripts of every level are still defined (the bottom level is skipped when it
    // runs a private copy).
    bool AllScriptsStillDefined();
    // 0x00472940: whether it has code to run.
    bool HasAttachedScript();
    // 0x00472F90: m_spawnerFile = the .qb file of `spawner`'s bottom-level script (SpawnScript's CFunction).
    void SetSpawnerFile(CScript* spawner);
    // 0x00473BE0: `object` is going: let go of it at every level, and stop waiting.
    void ObjectDestroyed(GameObjects::CEntity* object);
    // 0x00472ED0 / 0x00472EE0: the OnExceptionRun script. 0x00472EF0: the OnExitRun script.
    void SetOnExceptionScript(uint32_t name);
    uint32_t GetOnExceptionScript();
    void SetOnExitScript(uint32_t name);
    // 0x00472F00: no OnExitRun script at any level.
    void ClearOnExitScripts();

    // --- the interpreter (group B: cscript_exec.cpp) ---
    // 0x004750B0: run statements until it waits or ends.
    EScriptOutcome TickFrame();
    // 0x00474B40: one statement: a C function / member function / script call (also `obj:Func`), an
    // assignment `name = value`, or an expression (its truth value). Returns the call's result.
    bool ExecuteCommand();
    // 0x00475050: if / if not (token 0x47): run the condition, skip to the else / endif when false.
    void ExecuteIf();
    // 0x00473C60: switch (token 0x3C) to the matching case (or past endswitch).
    void ExecuteSwitch();
    // 0x00472A70: past the endswitch matching the switch just entered (nested switches skipped).
    void SkipToEndOfSwitch();
    // 0x00473220 / 0x00472AB0 / 0x00472B50: begin, repeat [n], break.
    void ExecuteBegin();
    void ExecuteRepeat();
    void ExecuteBreak();
    // 0x00472950: the rest of the line as m_functionParams.
    void ParseFunctionParams();
    // 0x004729E0: the name at m_pc (a name token, or <name> / <...> from the parameters), or 0.
    uint32_t ReadName();
    // 0x004731A0 / 0x004731E0: a member function on `object` (null: m_entity; none: false) - by table entry
    // or by name.
    bool InvokeMethodByName(CMemberFunctionEntry* entry, GameObjects::CEntity* object);
    bool InvokeMethodByName(uint32_t name, GameObjects::CEntity* object);

    // --- waiting, call stack, events (group C: cscript_wait.cpp) ---
    // 0x00473050: count down / check this Update's wait (see EBlockReason).
    void UpdateWait();
    // 0x004728F0: stop waiting. 0x00472900: whether blocked (`print`: prints "m_wait_type == %i").
    void StopWaiting();
    bool IsBlocked(bool print);
    // 0x00472BD0 / 0x00472BF0: wait `frames` Updates (one per frame: SUSPEND_ON_FRAME).
    void Stall(int frames);
    void SuspendUntilNextFrame(int frames);
    // 0x00472C10: wait `ms` of frame time (as seconds, * 0.001f). 0x00473310: `ms` of the game clock.
    void Lull(int ms);
    void WaitRealTime(int ms);
    // 0x00472FD0: block until an event of type `eventType` (0: until unblocked); registers with the tracker.
    void GateOff(uint32_t eventType);
    // 0x004739A0: wait (type 5-10) until `component` says so; marks the component (+0x08 |= 2).
    void SetWaitOnComponent(EBlockReason type, GameObjects::CObjectModule* component, uint32_t arg);
    // 0x00472D20: `component` is going: no level waits on it any more.
    void ComponentDestroyed(GameObjects::CObjectModule* component);
    // 0x00473800: an event: unblocks the levels blocked on its type, then the event handler table gets it.
    // Always true.
    bool ReceiveEvent(GameObjects::CSignal* event);
    // 0x00472F40: the level an interrupt interrupted stops waiting (the CFunction UnBlockInterruptedScript).
    void UnblockInterruptedScript();
    // 0x004748E0: call `name` (`code`; its default parameters plus `params`, on `object`): the current state
    // goes on the call stack. With no code running, just BindScript. `interrupted`: returning ends Update (5).
    void RunSubScript(uint32_t name, const uint8_t* code, CStruct* params, GameObjects::CEntity* object, bool interrupted);
    // 0x00473E70: '$' / return: release the code, pop the caller's state (none: the script ends); `results`
    // (the return's parameters) are added to the caller's parameters. Returns whether the level that ended
    // was an interrupt.
    bool ReturnFromScript(CStruct* results);
    // 0x00475750: run the cached script `name` now, as an interrupt, then carry on Updating.
    EScriptOutcome Preempt(uint32_t name, CStruct* params);
    // Event handlers (0x00472EA0, 0x00472EC0, 0x004734E0, 0x004735D0, 0x00473690, 0x004736C0): through
    // m_eventHandlers (made on demand: 0x10 bytes, 0x0045F820, on the front-end heap).
    void ClearEventHandlerGroup(uint32_t group);                       // 0x00472EA0 (0x004600E0)
    int PrintEventHandlerTable();                                      // 0x00472EC0 (0x0045F880, cdecl)
    void InstallEventTable(CArray* handlers, bool replace);             // 0x004734E0
    void SetEventHandler(uint32_t eventType, uint32_t script, uint32_t group, bool exception,
                         CStruct* params);                             // 0x004735D0
    void ClearEventHandler(uint32_t eventType);                        // 0x00473690
    void ResetEventHandlersFromTable(CArray* table, uint32_t group);   // 0x004736C0

    // --- the fields ---
    CScript* m_following;                 // +0x00 the list (older)
    CScript* m_priorEntry;             // +0x04 (newer; null for the head)
    uint8_t* m_codeCopy;              // +0x08 a private copy of the code (SetScriptCopy), freed by ResetScriptState
    const uint8_t* m_pc;              // +0x0C the next token; null: not running
    CStruct* m_functionParams;        // +0x10 the current statement's arguments (owned)
    CStruct* m_args;                // +0x14 the script's parameters (owned; CFunctions leave results here)
    SRepeatFrame* m_currentLoop;             // +0x18 the innermost begin ... repeat (null: none)
    SRepeatFrame m_inlineLoop;               // +0x1C
    SRepeatFrame* m_loops;                   // +0x2C m_inlineLoop, or 10 on the script heap
    int m_numReturnAddresses;         // +0x30 the call depth
    SCallFrame m_inlineReturnAddresses[2]; // +0x34
    SCallFrame* m_returnAddresses; // +0x94 m_inlineReturnAddresses, or 16 on the script heap
    int m_waitCounter;                // +0x98 SUSPEND_ON_COUNT / ONE_PER_FRAME
    bool m_waitTimerStarted;          // +0x9C SUSPEND_ON_TIMER: the first Update has been
    uint8_t unknown9D[3];
    float m_waitTimer;                // +0xA0 SUSPEND_ON_TIMER seconds left / REAL_TIMER ms
    uint32_t m_waitType;              // +0xA4 EBlockReason
    uint32_t m_waitArg;               // +0xA8 BLOCKED: the event type; REAL_TIMER: the start; 5-10: for the component
    GameObjects::CObjectModule* m_waitComponent; // +0xAC types 5-10
    uint32_t m_uniqueId;              // +0xB0 from 0x006F3540
    uint8_t m_flagBits;                  // +0xB4 SCRIPT_RUN_*
    uint8_t unknownB5[3];
    int m_listLink;                       // +0xB8 SpawnScript's node
    GameObjects::CSharedRef<GameObjects::CEntity> m_entity; // +0xBC the object it runs on (+0xC0 / +0xC4 the pointer's links)
    uint32_t m_scriptTitle;            // +0xC8 the script running now (the top level)
    uint8_t m_spawnFlags;             // +0xCC SCRIPT_FLAG_*
    uint8_t unknownCD[3];
    uint32_t m_spawnerFile;           // +0xD0 the .qb file of the script that spawned it (SetSpawnerFile)
    uint32_t m_spawnId;               // +0xD4 SpawnScript's id
    uint32_t m_callbackScript;        // +0xD8 run when a spawned script ends
    CStruct* m_callbackParams;        // +0xDC (owned)
    GameObjects::CSignalRouteTable* m_eventHandlers; // +0xE0 made on demand
    uint32_t m_onExceptionScript;     // +0xE4 OnExceptionRun
    uint32_t m_onExitScript;          // +0xE8 OnExitRun (this level's)
    GameObjects::CSharedBase m_refCounted;    // +0xEC lets smart pointers point at the script (vtable 0x006478E4)
};
static_assert(offsetof(CScript, m_priorEntry) == 0x04 && offsetof(CScript, m_codeCopy) == 0x08 &&
                  offsetof(CScript, m_pc) == 0x0C && offsetof(CScript, m_functionParams) == 0x10 &&
                  offsetof(CScript, m_args) == 0x14 && offsetof(CScript, m_currentLoop) == 0x18 &&
                  offsetof(CScript, m_inlineLoop) == 0x1C && offsetof(CScript, m_loops) == 0x2C &&
                  offsetof(CScript, m_numReturnAddresses) == 0x30 &&
                  offsetof(CScript, m_inlineReturnAddresses) == 0x34 && offsetof(CScript, m_returnAddresses) == 0x94,
              "layout must match the original");
static_assert(offsetof(CScript, m_waitCounter) == 0x98 && offsetof(CScript, m_waitTimerStarted) == 0x9C &&
                  offsetof(CScript, m_waitTimer) == 0xA0 && offsetof(CScript, m_waitType) == 0xA4 &&
                  offsetof(CScript, m_waitArg) == 0xA8 && offsetof(CScript, m_waitComponent) == 0xAC &&
                  offsetof(CScript, m_uniqueId) == 0xB0 && offsetof(CScript, m_flagBits) == 0xB4 &&
                  offsetof(CScript, m_listLink) == 0xB8 && offsetof(CScript, m_entity) == 0xBC,
              "layout must match the original");
static_assert(offsetof(CScript, m_scriptTitle) == 0xC8 && offsetof(CScript, m_spawnFlags) == 0xCC &&
                  offsetof(CScript, m_spawnerFile) == 0xD0 && offsetof(CScript, m_spawnId) == 0xD4 &&
                  offsetof(CScript, m_callbackScript) == 0xD8 && offsetof(CScript, m_callbackParams) == 0xDC &&
                  offsetof(CScript, m_eventHandlers) == 0xE0 && offsetof(CScript, m_onExceptionScript) == 0xE4 &&
                  offsetof(CScript, m_onExitScript) == 0xE8 && offsetof(CScript, m_refCounted) == 0xEC,
              "layout must match the original");
static_assert(sizeof(CScript) == 0xF4, "allocated as 0xF4 bytes by the original");

// m_flagBits (+0xB4)
constexpr uint8_t SCRIPT_RUN_INTERRUPTED = 0x01;   // the top level was entered as an interrupt
constexpr uint8_t SCRIPT_RUN_WAIT_UPDATED = 0x02;  // the wait was already counted this Update (set on return)
// m_spawnFlags (+0xCC)
constexpr uint8_t SCRIPT_FLAG_SPAWNED = 0x01;
constexpr uint8_t SCRIPT_FLAG_NOT_SESSION_SPECIFIC = 0x02; // survives KillAllChildScripts
constexpr uint8_t SCRIPT_FLAG_PAUSED = 0x04;
constexpr uint8_t SCRIPT_FLAG_PAUSE_WITH_OBJECT = 0x08;    // not updated while its object says no (vtable +0x1C)
constexpr uint8_t SCRIPT_FLAG_TRACKER_BLOCKED = 0x10;      // Block registered it with the tracker

// The globals.
extern CScript* g_scriptListHead;
extern CScript* g_currentScript;
extern int g_scriptCount;
extern uint32_t g_nextScriptUniqueId;
extern bool g_updatingSpawnedScripts;
extern bool g_deleteSpawnedScriptsPending;
extern bool g_onePerFrameReleased; // SUSPEND_ON_FRAME
extern MemSys::PoolPair g_scriptPools;
inline CScript*& ScriptListHead() { return g_scriptListHead; }
inline CScript*& CurrentScriptVar() { return g_currentScript; } // GetRunningScript 0x004728C0
inline int& ScriptCount() { return g_scriptCount; }
inline uint32_t& NextScriptUniqueId() { return g_nextScriptUniqueId; }
inline bool& UpdatingSpawnedScripts() { return g_updatingSpawnedScripts; }
inline bool& DeleteSpawnedScriptsPending() { return g_deleteSpawnedScriptsPending; }
inline bool& OnePerFrameReleased() { return g_onePerFrameReleased; }
// (also: the CScript pool pair 0x006F3548 - qb.cpp creates it; the script cache 0x006F3558 - cache.h; the object
// tracker 0x006F0FC0; OnlinePlay::Hub 0x007CCE14; the game clock offset 0x00696D60)

// --- free functions ---
// 0x00472980 (group B): a member function on `object` (no null check): the object itself for the two special
// component types (vtable +0x08), else its component of that type (0x0045DB90; vtable +0x2C).
bool InvokeMethodByName(CMemberFunctionEntry* entry, CStruct* params, GameObjects::CEntity* object, CScript* script);
// 0x004734B0 (group A): the script with this unique id, or null.
CScript* FindScriptWithUniqueId(uint32_t id);

// The script manager (group D: cscript_manager.cpp).
// 0x00475920: one Update of every spawned script (newest first); finished ones run their callback and go.
void TickChildScripts();
// 0x004753C0: delete the spawned scripts that aren't SCRIPT_FLAG_NOT_SESSION_SPECIFIC (deferred to the end of
// TickChildScripts if called during it).
void KillAllChildScripts();
// 0x00475420 / 0x00475670: spawn `name` (a C function is just called, with a temporary script; returns null).
CScript* SpawnScript(uint32_t name, CStruct* params, uint32_t callback, CStruct* callbackParams, int node,
                     uint32_t id, bool replicateFlag, bool permanent, bool notSessionSpecific, bool pauseWithObject);
CScript* SpawnScript(const char* name, CStruct* params, uint32_t callback, CStruct* callbackParams, int node,
                     uint32_t id, bool replicateFlag, bool permanent, bool notSessionSpecific, bool pauseWithObject);
// 0x004756C0: delete `script` if it's spawned.
void KillSpawnedScript(CScript* script);
// 0x00475790 / 0x004758F0: run `name` to the end now (a C function / member function is just called); `name`
// (the string) isn't used. With `replicateFlag` in a network game, the other players are told (0x00473350).
void LaunchScript(uint32_t name, CStruct* params, GameObjects::CEntity* object, bool replicateFlag, const char* nameString);
void LaunchScript(const char* name, CStruct* params, GameObjects::CEntity* object, bool replicateFlag);
// 0x004752F0: one Update of `name`, then its parameters are added to `results` (not checked for null).
void RunScriptOnce(uint32_t name, CStruct* params, GameObjects::CEntity* object, CStruct* results);
// 0x00473350 / 0x00473420: the network messages for LaunchScript (0x48) and SpawnScript (0x49).
void SendRunScriptMessage(uint32_t name, CStruct* params, GameObjects::CEntity* object);
void SendSpawnScriptMessage(uint32_t name, GameObjects::CEntity* object, int node, bool permanent);
// 0x00472D80 / 0x00472DC0: pause or unpause every spawned script / unpause one.
void PauseSpawnedScripts(bool pause);
void ResumeChildScript(CScript* script);
// 0x00472DE0: whether `name` is a script. 0x00472E00: `script`'s spawn id (0 if not spawned).
bool ScriptExists(uint32_t name);
uint32_t GetSpawnId(CScript* script);
// 0x00472E20: the spawned script with spawn id `id`, or null.
CScript* FindSpawnedScriptWithId(uint32_t id);
// 0x00472E70 (group C): ComponentDestroyed on every script.
void ComponentDestroyedAllScripts(GameObjects::CObjectModule* component);
// (group C) 0x00472D20's entry point: CScript::ComponentDestroyed keeping ECX as it was - the original's caller
// 0x00472E70 reads ECX after the call.
void ComponentDestroyedStub();
// Stopping scripts (ResetScriptState + ReportStopped; they stay on the list, TickChildScripts deletes spawned
// ones once Update says finished):
void StopScriptsTargeting(uint32_t name);                               // 0x00473FA0 (any level)
void StopScriptsByTag(uint32_t id);              // 0x00474040 (spawn id, unique id or spawner file)
void StopScriptsOfObjectAndTag(GameObjects::CEntity* object, uint32_t id, CScript* except);     // 0x004740D0
void EndChildScriptsOfEntityByName(GameObjects::CEntity* object, uint32_t name, CScript* except); // 0x00474190
void StopAllScripts();                                                           // 0x00474250 (every script)
void StopScriptsUsingObject(GameObjects::CEntity* object, uint32_t name);  // 0x004742B0 (name 0: any; top level only)
void EndScriptsBoundToEntity(GameObjects::CEntity* object);          // 0x004756F0 = 0x004742B0(object, 0)
void StopScriptsUsingObjectThatReferTo(GameObjects::CEntity* object, uint32_t name); // 0x00474350 (name at any level)
// 0x00475700: delete the scripts that aren't spawned, have no code and no event handler table (Cleanup).
void DeleteFinishedScripts();
} // namespace Script
