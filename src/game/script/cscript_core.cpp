// CScript's life: construction and destruction, the pooled new / delete, stopping (ClearScript), setting the
// script to run, and the small accessors. See cscript.h for the class and docs/script-runtime.md.

#include "game/script/cscript.h"

#include <cstring>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/script/cache.h"
#include "game/script/parse.h"
#include "game/script/qb.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Script {

namespace {

// The CScript pool pair (300 items, made by the script system's start-up).
Mem::PoolPair& ScriptPools() { return GameVariable<Mem::PoolPair>(0x006F3548); }

// A new empty structure from the structure pool (null if there's no room).
CStruct* NewEmptyStruct()
{
    CStruct* structure = NewStruct();
    return structure ? structure->Init() : nullptr;
}
// `delete structure` (its destructor is Clear).
void FreeStruct(CStruct* structure)
{
    structure->Clear();
    DeleteStruct(structure);
}


// Not rewritten yet.
// Obj::CRefCounted's constructor (vtable 0x006478E4, no smart pointers) and destructor (0x00463820 after
// putting the vtable back).
void ConstructRefCounted(Obj::CRefCounted* object)
{
    GameFunction<Obj::CRefCounted*(__thiscall*)(Obj::CRefCounted*)>(0x004637B0)(object);
}
void DestroyRefCounted(Obj::CRefCounted* object) { GameFunction<void(__thiscall*)(Obj::CRefCounted*)>(0x00463870)(object); }
// The event handler table: forget `script` (its handlers' references to it), and handle an event of `type`.
void ForgetScript(Obj::CEventHandlerTable* table, CScript* script)
{
    GameFunction<void(__thiscall*)(Obj::CEventHandlerTable*, CScript*)>(0x0045FA60)(table, script);
}
void PassEvent(Obj::CEventHandlerTable* table, uint32_t type)
{
    GameFunction<void(__thiscall*)(Obj::CEventHandlerTable*, uint32_t)>(0x0045FE60)(table, type);
}
// The event handler table's destructor (frees every handler's parameters, then its array).
void DestroyEventHandlerTable(Obj::CEventHandlerTable* table)
{
    GameFunction<void(__thiscall*)(Obj::CEventHandlerTable*)>(0x0045FB70)(table);
}
// The object tracker (0x006F0FC0): `script` no longer blocks on an event it registered for.
void TrackerForgetScript(CScript* script)
{
    GameFunction<void(__thiscall*)(void*, CScript*)>(0x00462B90)(GameVariable<void*>(0x006F0FC0), script);
}

// The event a stopped script sends its own event handler table (0x8B713E0E).
constexpr uint32_t kScriptStoppedEvent = 0x8B713E0E;

} // namespace

// 0x004738E0
CScript* CScript::Construct()
{
    m_object.mp_prev = nullptr;
    m_object.mp_next = nullptr;
    m_object.mp_object = nullptr;
    ConstructRefCounted(&m_refCounted);
    // Then all of it is zeroed - m_refCounted's vtable too (an original quirk, kept).
    std::memset(this, 0, sizeof(CScript));
    mp_next = ScriptListHead();
    if (ScriptListHead())
        ScriptListHead()->mp_previous = this;
    m_uniqueId = NextScriptUniqueId();
    ++ScriptCount();
    ++NextScriptUniqueId();
    m_node = 0;
    m_eventHandlers = nullptr;
    m_returnAddresses = m_inlineReturnAddresses;
    m_onExceptionScript = 0;
    m_onExitScript = 0;
    m_loops = &m_inlineLoop;
    ScriptListHead() = this;
    return this;
}

// 0x00474420
void CScript::Destroy()
{
    ClearScript();
    ReportStopped(); // (inlined in the original)
    if (m_callbackParams)
        FreeStruct(m_callbackParams);

    if (!mp_previous)
        ScriptListHead() = mp_next;
    if (mp_next)
        mp_next->mp_previous = mp_previous;
    if (mp_previous)
        mp_previous->mp_next = mp_next;

    ForgetStoredRandomsOf(reinterpret_cast<uint32_t>(this));
    if (Obj::CEventHandlerTable* table = m_eventHandlers) {
        auto* bytes = reinterpret_cast<uint8_t*>(table);
        if (*reinterpret_cast<uint32_t*>(bytes + 0x0C)) {
            // Its handlers are being run: it is marked dead and freed afterwards.
            bytes[0x08] = 0;
        } else {
            DestroyEventHandlerTable(table);
            Mem::Manager::Get()->Free(table);
        }
    }
    --ScriptCount();
    DestroyRefCounted(&m_refCounted);
    m_object.Release();
}

// 0x00449970
void* CScript::operator new(size_t size) { return ScriptPools().Allocate(static_cast<unsigned int>(size)); }

// 0x004499B0
void CScript::operator delete(void* script) { ScriptPools().Free(script); }

// 0x00473A80
void CScript::ClearScript()
{
    RunOnExitScripts();
    if (m_codeCopy) {
        Mem::FreeMalloc(m_codeCopy);
        m_codeCopy = nullptr;
    }
    CScriptCache* cache = ScriptCache();
    // (released even when the code was a private copy or given to SetScript, which took no use of the cached
    // code: if that script is cached, a use someone else took is given back - an original quirk, kept)
    if (m_pc)
        cache->ReleaseScript(m_scriptName);
    m_pc = nullptr;
    if (m_params) {
        FreeStruct(m_params);
        m_params = nullptr;
    }
    if (m_functionParams) {
        FreeStruct(m_functionParams);
        m_functionParams = nullptr;
    }
    for (int i = 0; i < m_numReturnAddresses; ++i) {
        cache->ReleaseScript(m_returnAddresses[i].mScriptName);
        if (CStruct* params = m_returnAddresses[i].mpParams) {
            FreeStruct(params);
            m_returnAddresses[i].mpParams = nullptr;
        }
    }
    m_numReturnAddresses = 0;
    if (m_returnAddresses != m_inlineReturnAddresses)
        Mem::FreeMalloc(m_returnAddresses);
    m_returnAddresses = m_inlineReturnAddresses;
    m_currentLoop = nullptr;
    if (m_loops != &m_inlineLoop)
        Mem::FreeMalloc(m_loops);
    m_loops = &m_inlineLoop;
    m_waitCounter = 0;
    m_waitType = WAIT_TYPE_NONE;
    m_waitComponent = nullptr;
    m_waitArg = 0;
    m_object.Release();
    m_scriptName = 0;
    m_flags &= ~(SCRIPT_RUN_INTERRUPTED | SCRIPT_RUN_WAIT_UPDATED);
}

// 0x00473880
void CScript::RunOnExitScripts()
{
    if (uint32_t name = m_onExitScript) {
        m_onExitScript = 0;
        Interrupt(name, nullptr);
    }
    // From the top level down; the depth is read once (the interrupts push and pop levels of their own).
    for (int i = m_numReturnAddresses; i; --i) {
        uint32_t& onExit = m_returnAddresses[i - 1].mOnExitScript;
        if (uint32_t name = onExit) {
            onExit = 0;
            Interrupt(name, nullptr);
        }
    }
}

// 0x00473010
void CScript::ReportStopped()
{
    if (m_eventHandlers) {
        ForgetScript(m_eventHandlers, this);
        PassEvent(m_eventHandlers, kScriptStoppedEvent);
    }
    if (m_spawnFlags & SCRIPT_FLAG_TRACKER_BLOCKED)
        TrackerForgetScript(this);
}

// 0x004739D0
CStruct* CScript::CopyParamsIfOwned(CStruct* params)
{
    if (!params)
        return nullptr;
    if ((m_functionParams && m_functionParams->References(params)) || (m_params && m_params->References(params))) {
        CStruct* copy = NewEmptyStruct();
        copy->AppendStructureIfAny(params); // (no check for a full pool)
        return copy;
    }
    return nullptr;
}

// 0x00474540
void CScript::SetScript(uint32_t name, const uint8_t* code, CStruct* params, Obj::CObject* object)
{
    // ClearScript frees this script's parameters: keep what `params` needs.
    CStruct* copy = CopyParamsIfOwned(params);
    ClearScript();
    m_functionParams = NewEmptyStruct();
    m_scriptName = name;
    m_object.Set(object);
    m_params = NewEmptyStruct();
    m_pc = code ? code : ScriptCache()->GetScript(name);
    if (!m_pc) {
        // No such script.
        if (copy)
            FreeStruct(copy);
        ClearScript();
        ReportStopped();
        return;
    }
    // The first line: the script's default parameters.
    m_pc = ParseArguments(m_params, m_pc, nullptr);
    if (copy) {
        m_params->AppendStructureIfAny(copy);
        FreeStruct(copy);
    } else {
        m_params->AppendStructureIfAny(params);
    }
}

// 0x004747A0
void CScript::SetScript(uint32_t name, CStruct* params, Obj::CObject* object) { SetScript(name, nullptr, params, object); }

// 0x004747C0
void CScript::SetScript(const char* name, CStruct* params, Obj::CObject* object)
{
    SetScript(Crc::GenerateCRCFromString(name), nullptr, params, object);
}

// 0x00474720
void CScript::SetScriptCopy(SStructScript* script, CStruct* params, Obj::CObject* object)
{
    auto size = static_cast<uint32_t>(SkipToEndOfScript(script->mpScriptTokens) - script->mpScriptTokens);
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_scriptHeap);
    auto* copy = static_cast<uint8_t*>(Mem::Malloc(size));
    Mem::Manager::Get()->PopContext();
    const uint8_t* source = script->mpScriptTokens;
    for (uint32_t i = 0; i < size; ++i)
        copy[i] = source[i];
    SetScript(script->mNameChecksum, copy, params, object);
    m_codeCopy = copy; // (after SetScript: its ClearScript would free it)
}

// 0x004747F0
void CScript::Restart()
{
    CStruct* params = NewEmptyStruct();
    uint32_t name;
    Obj::CObject* object;
    if (m_numReturnAddresses) {
        // The bottom level: the script it was started with.
        params->Assign(m_returnAddresses[0].mpParams);
        object = m_returnAddresses[0].mpObject;
        name = m_returnAddresses[0].mScriptName;
    } else {
        params->Assign(m_params);
        object = m_object.Get();
        name = m_scriptName;
    }
    SetScript(name, nullptr, params, object);
    if (params)
        FreeStruct(params);
}

// 0x00472C90
bool CScript::AllScriptsStillDefined()
{
    if (m_numReturnAddresses == 0)
        return m_codeCopy || FindSymbol(m_scriptName);
    if (!FindSymbol(m_scriptName))
        return false;
    // (the bottom level's script isn't looked for when it runs a private copy)
    for (int i = m_codeCopy ? 1 : 0; i < m_numReturnAddresses; ++i) {
        if (!FindSymbol(m_returnAddresses[i].mScriptName))
            return false;
    }
    return true;
}

// 0x00472940
bool CScript::GotScript() { return m_pc != nullptr; }

// 0x00472F90
void CScript::SetSpawnerFile(CScript* spawner)
{
    uint32_t name = spawner->m_numReturnAddresses ? spawner->m_returnAddresses[0].mScriptName : spawner->m_scriptName;
    m_spawnerFile = FindSymbol(name)->unknown8; // (no check that it is still defined)
}

// 0x00473BE0
void CScript::ObjectDestroyed(Obj::CObject* object)
{
    if (!object)
        return;
    if (m_object.Get() == object)
        m_object.Release();
    for (int i = 0; i < m_numReturnAddresses; ++i) {
        if (m_returnAddresses[i].mpObject == object)
            m_returnAddresses[i].mpObject = nullptr;
    }
    m_waitType = WAIT_TYPE_NONE; // (whatever it was waiting for)
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
    for (CScript* script = ScriptListHead(); script; script = script->mp_next) {
        if (script->m_uniqueId == id)
            return script;
    }
    return nullptr;
}

} // namespace Script

REPLACE(0x004738E0, &Script::CScript::Construct);
REPLACE(0x00474420, &Script::CScript::Destroy);
REPLACE(0x00449970, static_cast<void* (*)(size_t)>(&Script::CScript::operator new));
REPLACE(0x004499B0, static_cast<void (*)(void*)>(&Script::CScript::operator delete)); // (noexcept stripped)
REPLACE(0x00473A80, &Script::CScript::ClearScript);
REPLACE(0x00473880, &Script::CScript::RunOnExitScripts);
REPLACE(0x00473010, &Script::CScript::ReportStopped);
REPLACE(0x004739D0, &Script::CScript::CopyParamsIfOwned);
REPLACE(0x00474540,
        static_cast<void (Script::CScript::*)(uint32_t, const uint8_t*, Script::CStruct*, Obj::CObject*)>(
            &Script::CScript::SetScript));
REPLACE(0x004747A0, static_cast<void (Script::CScript::*)(uint32_t, Script::CStruct*, Obj::CObject*)>(
                        &Script::CScript::SetScript));
REPLACE(0x004747C0, static_cast<void (Script::CScript::*)(const char*, Script::CStruct*, Obj::CObject*)>(
                        &Script::CScript::SetScript));
REPLACE(0x00474720, &Script::CScript::SetScriptCopy);
REPLACE(0x004747F0, &Script::CScript::Restart);
REPLACE(0x00472C90, &Script::CScript::AllScriptsStillDefined);
REPLACE(0x00472940, &Script::CScript::GotScript);
REPLACE(0x00472F90, &Script::CScript::SetSpawnerFile);
REPLACE(0x00473BE0, &Script::CScript::ObjectDestroyed);
REPLACE(0x00472ED0, &Script::CScript::SetOnExceptionScript);
REPLACE(0x00472EE0, &Script::CScript::GetOnExceptionScript);
REPLACE(0x00472EF0, &Script::CScript::SetOnExitScript);
REPLACE(0x00472F00, &Script::CScript::ClearOnExitScripts);
REPLACE(0x004734B0, Script::FindScriptWithUniqueId);
