// CScript's life: construction and destruction, the pooled new / delete, stopping (ResetScriptState), setting the
// script to run, and the small accessors. See cscript.h for the class and docs/script-runtime.md.

#include "game/script/cscript.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "game/core/checksums.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/slab_bank.h"
#include "game/obj/signal.h"
#include "game/obj/tracker.h"
#include "game/script/cache.h"
#include "game/script/token_reader.h"
#include "game/script/qb.h"
#include "game/script/param_struct.h"

namespace Scripts {

CScript* g_currentScript;
CScript* g_scriptListHead;
bool g_onePerFrameReleased;
int g_scriptCount;
uint32_t g_nextScriptUniqueId;
bool g_updatingSpawnedScripts;
bool g_deleteSpawnedScriptsPending;
MemSys::PoolPair g_scriptPools;

namespace {

// The CScript pool pair (300 items, made by the script system's start-up).
MemSys::PoolPair& ScriptPools() { return g_scriptPools; }

// A new empty structure from the structure pool (null if there's no room).
CStruct* NewEmptyStruct()
{
    CStruct* structure = NewStruct();
    return structure ? structure->PrepareAll() : nullptr;
}
// `delete structure` (its destructor is Clear).
void FreeStruct(CStruct* structure)
{
    structure->Clear();
    DeleteStruct(structure);
}

} // namespace

// 0x004738E0
CScript* CScript::MakeNew()
{
    m_entity.m_preceding = nullptr;
    m_entity.m_following = nullptr;
    m_entity.m_ownerObject = nullptr;
    m_refCounted.MakeNew();
    // Then all of it is zeroed - m_refCounted's vtable too (an original quirk, kept).
    std::memset(this, 0, sizeof(CScript));
    m_following = ScriptListHead();
    if (ScriptListHead())
        ScriptListHead()->m_priorEntry = this;
    m_uniqueId = NextScriptUniqueId();
    ++ScriptCount();
    ++NextScriptUniqueId();
    m_listLink = 0;
    m_eventHandlers = nullptr;
    m_returnAddresses = m_inlineReturnAddresses;
    m_onExceptionScript = 0;
    m_onExitScript = 0;
    m_loops = &m_inlineLoop;
    ScriptListHead() = this;
    return this;
}

// 0x00474420
void CScript::Teardown()
{
    ResetScriptState();
    ReportStopped(); // (inlined in the original)
    if (m_callbackParams)
        FreeStruct(m_callbackParams);

    if (!m_priorEntry)
        ScriptListHead() = m_following;
    if (m_following)
        m_following->m_priorEntry = m_priorEntry;
    if (m_priorEntry)
        m_priorEntry->m_following = m_following;

    ForgetStoredRandomsOf(reinterpret_cast<uint32_t>(this));
    if (GameObjects::CSignalRouteTable* table = m_eventHandlers) {
        if (table->m_inUse) {
            // Its handlers are being run: it is marked dead and freed afterwards (by PassEvent).
            table->m_alive = false;
        } else {
            table->Teardown();
            MemSys::Hub::Pull()->Dealloc(table);
        }
    }
    --ScriptCount();
    m_refCounted.Destruct();
    m_entity.Release();
}

// 0x00449970
void* CScript::operator new(size_t size) { return ScriptPools().Carve(static_cast<unsigned int>(size)); }

// 0x004499B0
void CScript::operator delete(void* script) { ScriptPools().Dealloc(script); }

// 0x00473A80
void CScript::ResetScriptState()
{
    RunOnExitScripts();
    if (m_codeCopy) {
        MemSys::FreeMalloc(m_codeCopy);
        m_codeCopy = nullptr;
    }
    CScriptStore* cache = ScriptCache();
    // (released even when the code was a private copy or given to BindScript, which took no use of the cached
    // code: if that script is cached, a use someone else took is given back - an original quirk, kept)
    if (m_pc)
        cache->ReleaseScript(m_scriptTitle);
    m_pc = nullptr;
    if (m_args) {
        FreeStruct(m_args);
        m_args = nullptr;
    }
    if (m_functionParams) {
        FreeStruct(m_functionParams);
        m_functionParams = nullptr;
    }
    for (int i = 0; i < m_numReturnAddresses; ++i) {
        cache->ReleaseScript(m_returnAddresses[i].m_scriptId);
        if (CStruct* params = m_returnAddresses[i].m_argStruct) {
            FreeStruct(params);
            m_returnAddresses[i].m_argStruct = nullptr;
        }
    }
    m_numReturnAddresses = 0;
    if (m_returnAddresses != m_inlineReturnAddresses)
        MemSys::FreeMalloc(m_returnAddresses);
    m_returnAddresses = m_inlineReturnAddresses;
    m_currentLoop = nullptr;
    if (m_loops != &m_inlineLoop)
        MemSys::FreeMalloc(m_loops);
    m_loops = &m_inlineLoop;
    m_waitCounter = 0;
    m_waitType = SUSPEND_ON_NOTHING;
    m_waitComponent = nullptr;
    m_waitArg = 0;
    m_entity.Release();
    m_scriptTitle = 0;
    m_flagBits &= ~(SCRIPT_RUN_INTERRUPTED | SCRIPT_RUN_WAIT_UPDATED);
}

// 0x00473880
void CScript::RunOnExitScripts()
{
    if (uint32_t name = m_onExitScript) {
        m_onExitScript = 0;
        Preempt(name, nullptr);
    }
    // From the top level down; the depth is read once (the interrupts push and pop levels of their own).
    for (int i = m_numReturnAddresses; i; --i) {
        uint32_t& onExit = m_returnAddresses[i - 1].mOnExitScript;
        if (uint32_t name = onExit) {
            onExit = 0;
            Preempt(name, nullptr);
        }
    }
}

// 0x00473010
void CScript::ReportStopped()
{
    if (m_eventHandlers) {
        m_eventHandlers->UnregisterWithTracker(this);
        m_eventHandlers->RemoveGroup(GameObjects::kAllGroups);
    }
    if (m_spawnFlags & SCRIPT_FLAG_TRACKER_BLOCKED)
        GameObjects::Tracker()->ForgetScript(this);
}

// 0x004739D0
CStruct* CScript::CopyParamsIfOwned(CStruct* params)
{
    if (!params)
        return nullptr;
    if ((m_functionParams && m_functionParams->RefTally(params)) || (m_args && m_args->RefTally(params))) {
        CStruct* copy = NewEmptyStruct();
        copy->AppendStructureIfAny(params); // (no check for a full pool)
        return copy;
    }
    return nullptr;
}

// 0x00474540
void CScript::BindScript(uint32_t name, const uint8_t* code, CStruct* params, GameObjects::CEntity* object)
{
    // ResetScriptState frees this script's parameters: keep what `params` needs.
    CStruct* copy = CopyParamsIfOwned(params);
    ResetScriptState();
    m_functionParams = NewEmptyStruct();
    m_scriptTitle = name;
    m_entity.AssignValue(object);
    m_args = NewEmptyStruct();
    m_pc = code ? code : ScriptCache()->FetchScript(name);
    if (!m_pc) {
        // No such script.
        if (copy)
            FreeStruct(copy);
        ResetScriptState();
        ReportStopped();
        return;
    }
    // The first line: the script's default parameters.
    m_pc = ParseArguments(m_args, m_pc, nullptr);
    if (copy) {
        m_args->AppendStructureIfAny(copy);
        FreeStruct(copy);
    } else {
        m_args->AppendStructureIfAny(params);
    }
}

// 0x004747A0
void CScript::BindScript(uint32_t name, CStruct* params, GameObjects::CEntity* object) { BindScript(name, nullptr, params, object); }

// 0x004747C0
void CScript::BindScript(const char* name, CStruct* params, GameObjects::CEntity* object)
{
    BindScript(Checksums::HashFromText(name), nullptr, params, object);
}

// 0x00474720
void CScript::SetScriptCopy(SParamScript* script, CStruct* params, GameObjects::CEntity* object)
{
    auto size = static_cast<uint32_t>(SkipToEndOfScript(script->m_codeStream) - script->m_codeStream);
    MemSys::Hub* manager = MemSys::Hub::Pull();
    manager->EnterHeapScope(manager->m_scriptHeap);
    auto* copy = static_cast<uint8_t*>(MemSys::AllocBlock(size));
    MemSys::Hub::Pull()->LeaveHeapScope();
    const uint8_t* source = script->m_codeStream;
    for (uint32_t i = 0; i < size; ++i)
        copy[i] = source[i];
    BindScript(script->m_ownNameHash, copy, params, object);
    m_codeCopy = copy; // (after BindScript: its ResetScriptState would free it)
}

// 0x004747F0
void CScript::Relaunch()
{
    CStruct* params = NewEmptyStruct();
    uint32_t name;
    GameObjects::CEntity* object;
    if (m_numReturnAddresses) {
        // The bottom level: the script it was started with.
        params->Assign(m_returnAddresses[0].m_argStruct);
        object = m_returnAddresses[0].m_ownerEntity;
        name = m_returnAddresses[0].m_scriptId;
    } else {
        params->Assign(m_args);
        object = m_entity.Pull();
        name = m_scriptTitle;
    }
    BindScript(name, nullptr, params, object);
    if (params)
        FreeStruct(params);
}

// 0x00472C90
bool CScript::AllScriptsStillDefined()
{
    if (m_numReturnAddresses == 0)
        return m_codeCopy || FindSymbol(m_scriptTitle);
    if (!FindSymbol(m_scriptTitle))
        return false;
    // (the bottom level's script isn't looked for when it runs a private copy)
    for (int i = m_codeCopy ? 1 : 0; i < m_numReturnAddresses; ++i) {
        if (!FindSymbol(m_returnAddresses[i].m_scriptId))
            return false;
    }
    return true;
}

// 0x00472940
bool CScript::HasAttachedScript() { return m_pc != nullptr; }

// 0x00472F90
void CScript::SetSpawnerFile(CScript* spawner)
{
    uint32_t name = spawner->m_numReturnAddresses ? spawner->m_returnAddresses[0].m_scriptId : spawner->m_scriptTitle;
    m_spawnerFile = FindSymbol(name)->unknown8; // (no check that it is still defined)
}

// 0x00473BE0
void CScript::ObjectDestroyed(GameObjects::CEntity* object)
{
    if (!object)
        return;
    if (m_entity.Pull() == object)
        m_entity.Release();
    for (int i = 0; i < m_numReturnAddresses; ++i) {
        if (m_returnAddresses[i].m_ownerEntity == object)
            m_returnAddresses[i].m_ownerEntity = nullptr;
    }
    m_waitType = SUSPEND_ON_NOTHING; // (whatever it was waiting for)
}

// 0x00472ED0 / 0x00472EE0 / 0x00472EF0
void CScript::SetOnExceptionScript(uint32_t name) { m_onExceptionScript = name; }
uint32_t CScript::GetOnExceptionScript() { return m_onExceptionScript; }
void CScript::SetOnExitScript(uint32_t name) { m_onExitScript = name; }

// 0x00472F00
void CScript::ClearOnExitScripts()
{
    m_onExitScript = 0;
    for (int i = m_numReturnAddresses; i; --i)
        m_returnAddresses[i - 1].mOnExitScript = 0;
}

// 0x004734B0
CScript* FindScriptWithUniqueId(uint32_t id)
{
    for (CScript* script = ScriptListHead(); script; script = script->m_following) {
        if (script->m_uniqueId == id)
            return script;
    }
    return nullptr;
}

} // namespace Script
